#define _XOPEN_SOURCE 500
#include <ftw.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>

#include <netlink.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/modem.h>
#include <ofono/log.h>

#define ACM_PATH              "/tty/ttyACM"
#define DEVICES_PATH          "/sys/devices/"
#define USB_PATH              "/dev/bus/usb/"
#define TELIT_VENDOR_ID       "1bc7"
#define TELIT_PRODUCT_ID      "0022"
#define VENDOR_ID             "idVendor"
#define PRODUCT_ID            "idProduct"
#define USB_DEVNUM            "devnum"
#define USB_DEVPATH           "devpath"
#define TTY_ACM               "ttyACM"
#define INTERFACE_NUMBER      "bInterfaceNumber"
#define DEVICES_PATH_MAX      0xFF
#define VPID_LEN              (sizeof(char) * 4)
#define VPID_FILE_NAME_SIZE   (sizeof(char) * 8)
#define TTY_ACM_SIZE          (sizeof(char) * 6)

struct modem_info {
   char * syspath;
   char * devname;
   char * driver;
   char * vendor;
   char * model;
   GSList * devices;
   struct ofono_modem *modem;
   const char * sysattr;
};
static struct modem_info * modem;

struct device_info {
   char * devpath;
   char * devnode;
   char * interface;
   char * number;
   char * label;
   char * sysattr;
};

static struct {
   const char *driver;
   const char *drv;
   const char *vid;
   const char *pid;
} vendor_list[] = {
   { "isiusb",     "cdc_phonet"                    },
   { "linktop",    "cdc_acm",      "230d"          },
   { "icera",      "cdc_acm",      "19d2"          },
   { "icera",      "cdc_ether",    "19d2"          },
   { "icera",      "cdc_acm",      "04e8", "6872"  },
   { "icera",      "cdc_ether",    "04e8", "6872"  },
   { "icera",      "cdc_acm",      "0421", "0633"  },
   { "icera",      "cdc_ether",    "0421", "0633"  },
   { "mbm",        "cdc_acm",      "0bdb"          },
   { "mbm",        "cdc_ether",    "0bdb"          },
   { "mbm",        "cdc_acm",      "0fce"          },
   { "mbm",        "cdc_ether",    "0fce"          },
   { "mbm",        "cdc_acm",      "413c"          },
   { "mbm",        "cdc_ether",    "413c"          },
   { "mbm",        "cdc_acm",      "03f0"          },
   { "mbm",        "cdc_ether",    "03f0"          },
   { "mbm",        "cdc_acm",      "0930"          },
   { "mbm",        "cdc_ether",    "0930"          },
   { "hso",        "hso"                           },
   { "gobi",       "qmi_wwan"                      },
   { "gobi",       "qcserial"                      },
   { "sierra",     "sierra"                        },
   { "sierra",     "sierra_net"                    },
   { "option",     "option",       "0af0"          },
   { "huawei",     "option",       "201e"          },
   { "huawei",     "cdc_wdm",      "12d1"          },
   { "huawei",     "cdc_ether",    "12d1"          },
   { "huawei",     "qmi_wwan",     "12d1"          },
   { "huawei",     "option",       "12d1"          },
   { "speedupcdma","option",       "1c9e", "9e00"  },
   { "speedup",    "option",       "1c9e"          },
   { "speedup",    "option",       "2020"          },
   { "alcatel",    "option",       "1bbb", "0017"  },
   { "novatel",    "option",       "1410"          },
   { "zte",        "option",       "19d2"          },
   { "simcom",     "option",       "05c6", "9000"  },
   { "telit",      "usbserial",    "1bc7"          },
   { "ge910",      "cdc_acm",      "1bc7", "0022"  },
   { "telit",      "option",       "1bc7"          },
   { "he910",      "cdc_acm",      "1bc7", "0021"  },
   { "nokia",      "option",       "0421", "060e"  },
   { "nokia",      "option",       "0421", "0623"  },
   { "samsung",    "option",       "04e8", "6889"  },
   { "samsung",    "kalmia"                        },
   { "quectel",    "option",       "05c6", "9090"  },
   { "ublox",      "cdc_acm",      "1546", "1102"  },
   { }
};

static gboolean setup_isi(struct modem_info *modem)
{
   const char *node = NULL;
   int addr = 0;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s %s", info->devnode, info->interface,
            info->number, info->label, info->sysattr);

      if (g_strcmp0(info->sysattr, "820") == 0) {
         if (g_strcmp0(info->interface, "2/254/0") == 0)
            addr = 16;

         node = info->devnode;
      }
   }

   if (node == NULL)
      return FALSE;

   DBG("interface=%s address=%d", node, addr);

   ofono_modem_set_string(modem->modem, "Interface", node);
   ofono_modem_set_integer(modem->modem, "Address", addr);

   return TRUE;
}

static gboolean setup_mbm(struct modem_info *modem)
{
   const char *mdm = NULL, *app = NULL, *network = NULL, *gps = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s %s", info->devnode, info->interface,
            info->number, info->label, info->sysattr);

      if (g_str_has_suffix(info->sysattr, "Modem") == TRUE ||
            g_str_has_suffix(info->sysattr,
               "Modem 2") == TRUE) {
         if (mdm == NULL)
            mdm = info->devnode;
         else
            app = info->devnode;
      } else if (g_str_has_suffix(info->sysattr,
               "GPS Port") == TRUE ||
            g_str_has_suffix(info->sysattr,
               "Module NMEA") == TRUE) {
         gps = info->devnode;
      } else if (g_str_has_suffix(info->sysattr,
               "Network Adapter") == TRUE ||
            g_str_has_suffix(info->sysattr,
               "NetworkAdapter") == TRUE) {
         network = info->devnode;
      }
   }

   if (mdm == NULL || app == NULL)
      return FALSE;

   DBG("modem=%s data=%s network=%s gps=%s", mdm, app, network, gps);

   ofono_modem_set_string(modem->modem, "ModemDevice", mdm);
   ofono_modem_set_string(modem->modem, "DataDevice", app);
   ofono_modem_set_string(modem->modem, "GPSDevice", gps);
   ofono_modem_set_string(modem->modem, "NetworkInterface", network);

   return TRUE;
}

static gboolean setup_hso(struct modem_info *modem)
{
   const char *ctl = NULL, *app = NULL, *mdm = NULL, *net = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s %s", info->devnode, info->interface,
            info->number, info->label, info->sysattr);

      if (g_strcmp0(info->sysattr, "Control") == 0)
         ctl = info->devnode;
      else if (g_strcmp0(info->sysattr, "Application") == 0)
         app = info->devnode;
      else if (g_strcmp0(info->sysattr, "Modem") == 0)
         mdm = info->devnode;
      else if (info->sysattr == NULL &&
            g_str_has_prefix(info->devnode, "hso") == TRUE)
         net = info->devnode;
   }

   if (ctl == NULL || app == NULL)
      return FALSE;

   DBG("control=%s application=%s modem=%s network=%s",
         ctl, app, mdm, net);

   ofono_modem_set_string(modem->modem, "Control", ctl);
   ofono_modem_set_string(modem->modem, "Application", app);
   ofono_modem_set_string(modem->modem, "Modem", mdm);
   ofono_modem_set_string(modem->modem, "NetworkInterface", net);

   return TRUE;
}

static gboolean setup_gobi(struct modem_info *modem)
{
   const char *qmi = NULL, *mdm = NULL, *net = NULL;
   const char *gps = NULL, *diag = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->interface, "255/255/255") == 0) {
         if (info->number == NULL)
            qmi = info->devnode;
         else if (g_strcmp0(info->number, "00") == 0)
            net = info->devnode;
         else if (g_strcmp0(info->number, "01") == 0)
            diag = info->devnode;
         else if (g_strcmp0(info->number, "02") == 0)
            mdm = info->devnode;
         else if (g_strcmp0(info->number, "03") == 0)
            gps = info->devnode;
      }
   }

   if (qmi == NULL || mdm == NULL || net == NULL)
      return FALSE;

   DBG("qmi=%s net=%s mdm=%s gps=%s diag=%s", qmi, net, mdm, gps, diag);

   ofono_modem_set_string(modem->modem, "Device", qmi);
   ofono_modem_set_string(modem->modem, "Modem", mdm);
   ofono_modem_set_string(modem->modem, "Diag", diag);
   ofono_modem_set_string(modem->modem, "NetworkInterface", net);

   return TRUE;
}

static gboolean setup_sierra(struct modem_info *modem)
{
   const char *mdm = NULL, *app = NULL, *net = NULL, *diag = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->interface, "255/255/255") == 0) {
         if (g_strcmp0(info->number, "01") == 0)
            diag = info->devnode;
         if (g_strcmp0(info->number, "03") == 0)
            mdm = info->devnode;
         else if (g_strcmp0(info->number, "04") == 0)
            app = info->devnode;
         else if (g_strcmp0(info->number, "07") == 0)
            net = info->devnode;
      }
   }

   if (mdm == NULL || net == NULL)
      return FALSE;

   DBG("modem=%s app=%s net=%s diag=%s", mdm, app, net, diag);

   ofono_modem_set_string(modem->modem, "Modem", mdm);
   ofono_modem_set_string(modem->modem, "App", app);
   ofono_modem_set_string(modem->modem, "Diag", diag);
   ofono_modem_set_string(modem->modem, "NetworkInterface", net);

   return TRUE;
}

static gboolean setup_option(struct modem_info *modem)
{
   const char *aux = NULL, *mdm = NULL, *diag = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->interface, "255/255/255") == 0) {
         if (g_strcmp0(info->number, "00") == 0)
            mdm = info->devnode;
         else if (g_strcmp0(info->number, "01") == 0)
            diag = info->devnode;
         else if (g_strcmp0(info->number, "02") == 0)
            aux = info->devnode;
      }

   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("aux=%s modem=%s diag=%s", aux, mdm, diag);

   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "Modem", mdm);
   ofono_modem_set_string(modem->modem, "Diag", diag);

   return TRUE;
}

static gboolean setup_huawei(struct modem_info *modem)
{
   const char *qmi = NULL, *mdm = NULL, *net = NULL;
   const char *pcui = NULL, *diag = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->label, "modem") == 0 ||
            g_strcmp0(info->interface, "255/1/1") == 0 ||
            g_strcmp0(info->interface, "255/2/1") == 0 ||
            g_strcmp0(info->interface, "255/1/49") == 0) {
         mdm = info->devnode;
      } else if (g_strcmp0(info->label, "pcui") == 0 ||
            g_strcmp0(info->interface, "255/1/2") == 0 ||
            g_strcmp0(info->interface, "255/2/2") == 0 ||
            g_strcmp0(info->interface, "255/1/50") == 0) {
         pcui = info->devnode;
      } else if (g_strcmp0(info->label, "diag") == 0 ||
            g_strcmp0(info->interface, "255/1/3") == 0 ||
            g_strcmp0(info->interface, "255/2/3") == 0 ||
            g_strcmp0(info->interface, "255/1/51") == 0) {
         diag = info->devnode;
      } else if (g_strcmp0(info->interface, "255/1/8") == 0 ||
            g_strcmp0(info->interface, "255/1/56") == 0) {
         net = info->devnode;
      } else if (g_strcmp0(info->interface, "255/1/9") == 0 ||
            g_strcmp0(info->interface, "255/1/57") == 0) {
         qmi = info->devnode;
      } else if (g_strcmp0(info->interface, "255/255/255") == 0) {
         if (g_strcmp0(info->number, "00") == 0)
            mdm = info->devnode;
         else if (g_strcmp0(info->number, "01") == 0)
            pcui = info->devnode;
         else if (g_strcmp0(info->number, "02") == 0)
            pcui = info->devnode;
         else if (g_strcmp0(info->number, "03") == 0)
            pcui = info->devnode;
         else if (g_strcmp0(info->number, "04") == 0)
            pcui = info->devnode;
      }
   }

   if (qmi != NULL && net != NULL) {
      ofono_modem_set_driver(modem->modem, "gobi");
      goto done;
   }

   if (mdm == NULL || pcui == NULL)
      return FALSE;

done:
   DBG("mdm=%s pcui=%s diag=%s qmi=%s net=%s", mdm, pcui, diag, qmi, net);

   ofono_modem_set_string(modem->modem, "Device", qmi);
   ofono_modem_set_string(modem->modem, "Modem", mdm);
   ofono_modem_set_string(modem->modem, "Pcui", pcui);
   ofono_modem_set_string(modem->modem, "Diag", diag);
   ofono_modem_set_string(modem->modem, "NetworkInterface", net);

   return TRUE;
}

static gboolean setup_speedup(struct modem_info *modem)
{
   const char *aux = NULL, *mdm = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->label, "aux") == 0) {
         aux = info->devnode;
         if (mdm != NULL)
            break;
      } else if (g_strcmp0(info->label, "modem") == 0) {
         mdm = info->devnode;
         if (aux != NULL)
            break;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("aux=%s modem=%s", aux, mdm);

   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "Modem", mdm);

   return TRUE;
}

static gboolean setup_linktop(struct modem_info *modem)
{
   const char *aux = NULL, *mdm = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->interface, "2/2/1") == 0) {
         if (g_strcmp0(info->number, "01") == 0)
            aux = info->devnode;
         else if (g_strcmp0(info->number, "03") == 0)
            mdm = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("aux=%s modem=%s", aux, mdm);

   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "Modem", mdm);

   return TRUE;
}

static gboolean setup_icera(struct modem_info *modem)
{
   const char *aux = NULL, *mdm = NULL, *net = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->interface, "2/2/1") == 0) {
         if (g_strcmp0(info->number, "00") == 0)
            aux = info->devnode;
         else if (g_strcmp0(info->number, "01") == 0)
            aux = info->devnode;
         else if (g_strcmp0(info->number, "02") == 0)
            mdm = info->devnode;
         else if (g_strcmp0(info->number, "03") == 0)
            mdm = info->devnode;
      } else if (g_strcmp0(info->interface, "2/6/0") == 0) {
         if (g_strcmp0(info->number, "05") == 0)
            net = info->devnode;
         else if (g_strcmp0(info->number, "06") == 0)
            net = info->devnode;
         else if (g_strcmp0(info->number, "07") == 0)
            net = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("aux=%s modem=%s net=%s", aux, mdm, net);

   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "Modem", mdm);
   ofono_modem_set_string(modem->modem, "NetworkInterface", net);

   return TRUE;
}

static gboolean setup_alcatel(struct modem_info *modem)
{
   const char *aux = NULL, *mdm = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->label, "aux") == 0) {
         aux = info->devnode;
         if (mdm != NULL)
            break;
      } else if (g_strcmp0(info->label, "modem") == 0) {
         mdm = info->devnode;
         if (aux != NULL)
            break;
      } else if (g_strcmp0(info->interface, "255/255/255") == 0) {
         if (g_strcmp0(info->number, "03") == 0)
            aux = info->devnode;
         else if (g_strcmp0(info->number, "05") == 0)
            mdm = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("aux=%s modem=%s", aux, mdm);

   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "Modem", mdm);

   return TRUE;
}

static gboolean setup_novatel(struct modem_info *modem)
{
   const char *aux = NULL, *mdm = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->label, "aux") == 0) {
         aux = info->devnode;
         if (mdm != NULL)
            break;
      } else if (g_strcmp0(info->label, "modem") == 0) {
         mdm = info->devnode;
         if (aux != NULL)
            break;
      } else if (g_strcmp0(info->interface, "255/255/255") == 0) {
         if (g_strcmp0(info->number, "00") == 0)
            aux = info->devnode;
         else if (g_strcmp0(info->number, "01") == 0)
            mdm = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("aux=%s modem=%s", aux, mdm);

   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "Modem", mdm);

   return TRUE;
}

static gboolean setup_nokia(struct modem_info *modem)
{
   const char *aux = NULL, *mdm = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->label, "aux") == 0) {
         aux = info->devnode;
         if (mdm != NULL)
            break;
      } else if (g_strcmp0(info->label, "modem") == 0) {
         mdm = info->devnode;
         if (aux != NULL)
            break;
      } else if (g_strcmp0(info->interface, "10/0/0") == 0) {
         if (g_strcmp0(info->number, "02") == 0)
            mdm = info->devnode;
         else if (g_strcmp0(info->number, "04") == 0)
            aux = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("aux=%s modem=%s", aux, mdm);

   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "Modem", mdm);

   return TRUE;
}

static gboolean setup_telit(struct modem_info *modem)
{
   const char *mdm = NULL, *aux = NULL, *gps = NULL, *diag = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s %s", info->devnode, info->interface,
            info->number, info->label, info->sysattr);

      if (g_strcmp0(info->label, "aux") == 0) {
         aux = info->devnode;
         if (mdm != NULL)
            break;
      } else if (g_strcmp0(info->label, "modem") == 0) {
         mdm = info->devnode;
         if (aux != NULL)
            break;
      } else if (g_strcmp0(info->interface, "255/255/255") == 0) {
         if (g_strcmp0(info->number, "00") == 0)
            mdm = info->devnode;
         else if (g_strcmp0(info->number, "01") == 0)
            diag = info->devnode;
         else if (g_strcmp0(info->number, "02") == 0)
            gps = info->devnode;
         else if (g_strcmp0(info->number, "03") == 0)
            aux = info->devnode;
      } else if (g_strcmp0(info->interface, "2/2/0") == 0) {
         if (g_strcmp0(info->number, "00") == 0)
            mdm = info->devnode;
         else if (g_strcmp0(info->number, "04") == 0)
            diag = info->devnode;
         else if (g_strcmp0(info->number, "02") == 0)
            aux = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("modem=%s aux=%s gps=%s diag=%s", mdm, aux, gps, diag);

   ofono_modem_set_string(modem->modem, "Modem", mdm);
   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "GPS", gps);

   return TRUE;
}

static gboolean setup_ge910(struct modem_info *modem)
{
   const char *mdm = NULL, *aux = NULL, *gps = NULL, *diag = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s %s", info->devnode, info->interface,
            info->number, info->label, info->sysattr);

      if (g_strcmp0(info->label, "aux") == 0) {
         aux = info->devnode;
         if (mdm != NULL)
            break;
      } else if (g_strcmp0(info->label, "modem") == 0) {
         mdm = info->devnode;
         if (aux != NULL)
            break;
      } else if (g_strcmp0(info->interface, "2/2/0") == 0) {
         if (g_strcmp0(info->number, "00") == 0)
            mdm = info->devnode;
         else if (g_strcmp0(info->number, "04") == 0)
            diag = info->devnode;
         else if (g_strcmp0(info->number, "02") == 0)
            aux = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("modem=%s aux=%s gps=%s diag=%s", mdm, aux, gps, diag);

   ofono_modem_set_string(modem->modem, "Modem", mdm);
   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "GPS", gps);

   return TRUE;
}

static gboolean setup_he910(struct modem_info *modem)
{
   const char *mdm = NULL, *aux = NULL, *gps = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->interface, "2/2/1") == 0) {
         if (g_strcmp0(info->number, "00") == 0)
            mdm = info->devnode;
         else if (g_strcmp0(info->number, "06") == 0)
            aux = info->devnode;
         else if (g_strcmp0(info->number, "0a") == 0)
            gps = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("modem=%s aux=%s gps=%s", mdm, aux, gps);

   ofono_modem_set_string(modem->modem, "Modem", mdm);
   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "GPS", gps);

   return TRUE;
}

static gboolean setup_simcom(struct modem_info *modem)
{
   const char *mdm = NULL, *aux = NULL, *gps = NULL, *diag = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->label, "aux") == 0) {
         aux = info->devnode;
         if (mdm != NULL)
            break;
      } else if (g_strcmp0(info->label, "modem") == 0) {
         mdm = info->devnode;
         if (aux != NULL)
            break;
      } else if (g_strcmp0(info->interface, "255/255/255") == 0) {
         if (g_strcmp0(info->number, "00") == 0)
            diag = info->devnode;
         else if (g_strcmp0(info->number, "01") == 0)
            gps = info->devnode;
         else if (g_strcmp0(info->number, "02") == 0)
            aux = info->devnode;
         else if (g_strcmp0(info->number, "03") == 0)
            mdm = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("modem=%s aux=%s gps=%s diag=%s", mdm, aux, gps, diag);

   ofono_modem_set_string(modem->modem, "Modem", mdm);
   ofono_modem_set_string(modem->modem, "Data", aux);
   ofono_modem_set_string(modem->modem, "GPS", gps);

   return TRUE;
}

static gboolean setup_zte(struct modem_info *modem)
{
   const char *aux = NULL, *mdm = NULL, *qcdm = NULL;
   const char *modem_intf;
   GSList *list;

   DBG("%s", modem->syspath);

   if (g_strcmp0(modem->model, "0016") == 0 ||
         g_strcmp0(modem->model, "0017") == 0 ||
         g_strcmp0(modem->model, "0117") == 0)
      modem_intf = "02";
   else
      modem_intf = "03";

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->label, "aux") == 0) {
         aux = info->devnode;
         if (mdm != NULL)
            break;
      } else if (g_strcmp0(info->label, "modem") == 0) {
         mdm = info->devnode;
         if (aux != NULL)
            break;
      } else if (g_strcmp0(info->interface, "255/255/255") == 0) {
         if (g_strcmp0(info->number, "00") == 0)
            qcdm = info->devnode;
         else if (g_strcmp0(info->number, "01") == 0)
            aux = info->devnode;
         else if (g_strcmp0(info->number, modem_intf) == 0)
            mdm = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("aux=%s modem=%s qcdm=%s", aux, mdm, qcdm);

   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "Modem", mdm);

   return TRUE;
}

static gboolean setup_samsung(struct modem_info *modem)
{
   const char *control = NULL, *network = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->interface, "10/0/0") == 0)
         control = info->devnode;
      else if (g_strcmp0(info->interface, "255/0/0") == 0)
         network = info->devnode;
   }

   if (control == NULL && network == NULL)
      return FALSE;

   DBG("control=%s network=%s", control, network);

   ofono_modem_set_string(modem->modem, "ControlPort", control);
   ofono_modem_set_string(modem->modem, "NetworkInterface", network);

   return TRUE;
}

static gboolean setup_quectel(struct modem_info *modem)
{
   const char *aux = NULL, *mdm = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->label, "aux") == 0) {
         aux = info->devnode;
         if (mdm != NULL)
            break;
      } else if (g_strcmp0(info->label, "modem") == 0) {
         mdm = info->devnode;
         if (aux != NULL)
            break;
      } else if (g_strcmp0(info->interface, "255/255/255") == 0) {
         if (g_strcmp0(info->number, "02") == 0)
            aux = info->devnode;
         else if (g_strcmp0(info->number, "03") == 0)
            mdm = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("aux=%s modem=%s", aux, mdm);

   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "Modem", mdm);

   return TRUE;
}

static gboolean setup_ublox(struct modem_info *modem)
{
   const char *aux = NULL, *mdm = NULL;
   GSList *list;

   DBG("%s", modem->syspath);

   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      DBG("%s %s %s %s", info->devnode, info->interface,
            info->number, info->label);

      if (g_strcmp0(info->label, "aux") == 0) {
         aux = info->devnode;
         if (mdm != NULL)
            break;
      } else if (g_strcmp0(info->label, "modem") == 0) {
         mdm = info->devnode;
         if (aux != NULL)
            break;
      } else if (g_strcmp0(info->interface, "2/2/1") == 0) {
         if (g_strcmp0(info->number, "02") == 0)
            aux = info->devnode;
         else if (g_strcmp0(info->number, "00") == 0)
            mdm = info->devnode;
      }
   }

   if (aux == NULL || mdm == NULL)
      return FALSE;

   DBG("aux=%s modem=%s", aux, mdm);

   ofono_modem_set_string(modem->modem, "Aux", aux);
   ofono_modem_set_string(modem->modem, "Modem", mdm);

   return TRUE;
}

static struct {
   const char *name;
   gboolean (*setup)(struct modem_info *modem);
   const char *sysattr;
} driver_list[] = {
   { "isiusb",     setup_isi,      "type"                  },
   { "mbm",        setup_mbm,      "device/interface"      },
   { "hso",        setup_hso,      "hsotype"               },
   { "gobi",       setup_gobi      },
   { "sierra",     setup_sierra    },
   { "option",     setup_option    },
   { "huawei",     setup_huawei    },
   { "speedupcdma",setup_speedup   },
   { "speedup",    setup_speedup   },
   { "linktop",    setup_linktop   },
   { "alcatel",    setup_alcatel   },
   { "novatel",    setup_novatel   },
   { "nokia",      setup_nokia     },
   { "telit",      setup_telit,    "device/interface"      },
   { "he910",      setup_he910     },
   { "ge910",      setup_ge910     },
   { "simcom",     setup_simcom    },
   { "zte",        setup_zte       },
   { "icera",      setup_icera     },
   { "samsung",    setup_samsung   },
   { "quectel",    setup_quectel   },
   { "ublox",      setup_ublox     },
   { }
};

static int add_devices(void);
static int remove_devices(void);
static void modem_info_delete(void);
static void device_info_delete(void);
static int find_modem_info_file(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf);
static int find_device_info_file(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf);
static void get_modem_info(void);
static gboolean get_device_info(void);
static char * read_data_in_file(const char * path);
static gboolean create_modem(gpointer key, gpointer value, gpointer user_data);
static void start_usb_detection(void);
static gboolean check_if_device_is_already_logged(char * devpath);
static gboolean check_if_device_is_already_unlogged(char * devpath);
static gboolean try_to_create_modem(gpointer data);

static gint mdev_device_add_driver(void);
static gint mdev_device_add_vendor_id(char * data);
static gint mdev_device_add_model_id(char * data);
static gint mdev_device_add_syspath_id(const char * data);
static gint mdev_device_add_devname(char * path);
static void mdev_device_add_devnode(const char * path, struct device_info * info);
static void mdev_device_add_devpath(const char * path, struct device_info * info);
static gint mdev_device_get_interface(const char * interface, struct device_info * info);
static void mdev_device_add_interface(struct device_info * info);
static gint mdev_device_add_number(struct device_info * info);
static gint mdev_device_add_label(struct device_info * info);
static gint mdev_device_add_sysattr(struct device_info * info);
static void mdev_device_add_info_in_list(struct device_info * info);
static gboolean mdev_event_on_usb(GIOChannel *channel, GIOCondition cond, gpointer user_data);
static int mdev_device_get_socket(void);

static gint compare_device(gconstpointer a, gconstpointer b)
{
   const struct device_info *info1 = a;
   const struct device_info *info2 = b;

   return g_strcmp0(info1->number, info2->number);
}

static gint mdev_device_add_driver(void)
{
   if (modem->vendor && modem->model) {
      gint i;
      for (i = 0; vendor_list[i].driver; i++) {
         if (vendor_list[i].vid) {
            if (strncmp(vendor_list[i].vid, TELIT_VENDOR_ID, VPID_LEN) == 0) {
               if (vendor_list[i].pid) {
                  if (strncmp(vendor_list[i].pid, TELIT_PRODUCT_ID, VPID_LEN) == 0) {
                     modem->driver = g_malloc(DEVICES_PATH_MAX);
                     g_stpcpy(modem->driver, vendor_list[i].driver);
                     return g_utf8_strlen(modem->driver, DEVICES_PATH_MAX);
                  }
               }
            }
         }
      }
   }

   return 0;
}

static gint mdev_device_add_vendor_id(char * data)
{
   gint i;

   for (i = 0; vendor_list[i].driver; i++) {
      if (vendor_list[i].vid) {
         if (strncmp(data, vendor_list[i].vid, VPID_LEN) == 0) {
            modem->vendor = data;
            return 1;
         }
      }
   }

   return 0;
}

static gint mdev_device_add_model_id(char * data)
{
   gint i;

   for (i = 0; vendor_list[i].driver; i++) {
      if (vendor_list[i].pid) {
         if (strncmp(data, vendor_list[i].pid, VPID_LEN) == 0) {
            modem->model = data;
            return 1;
         }
      }
   }

   return 0;
}

static gint mdev_device_add_syspath_id(const char * data)
{
   modem->syspath = g_strndup(data, g_utf8_strlen(data, DEVICES_PATH_MAX) - g_utf8_strlen(VENDOR_ID, DEVICES_PATH_MAX));
   return g_utf8_strlen(modem->syspath, DEVICES_PATH_MAX);
}

static gint mdev_device_get_devname(char * path, const char * usb_dev)
{
   gchar * tmp = g_strconcat(path, usb_dev, NULL);
   gchar * buf = read_data_in_file(tmp);
   gint ret = atoi(buf);

   g_free(tmp);
   g_free(buf);

   return ret;
}

static gint mdev_device_add_devname(char * path)
{
   gint devpath = mdev_device_get_devname(path, USB_DEVPATH);
   gint devnum = mdev_device_get_devname(path, USB_DEVNUM);

   modem->devname = (gchar *)g_malloc(sizeof(gchar) * (DEVICES_PATH_MAX));
   g_snprintf(modem->devname, DEVICES_PATH_MAX, USB_PATH"%03d/%03d", devpath, devnum);

   return 0;
}

static void mdev_device_add_devnode(const char * path, struct device_info * info)
{
   if (path != NULL)
      info->devnode = g_strconcat ("/dev/", path, NULL);
}

static void mdev_device_add_devpath(const char * path, struct device_info * info)
{
   if (path != NULL)
      info->devpath = g_strdup(path);
}

static gint mdev_device_get_interface(const char * interface, struct device_info * info)
{
   gchar * split = g_strndup(info->devpath, g_utf8_strlen(info->devpath, DEVICES_PATH_MAX) - g_utf8_strlen(info->devnode, DEVICES_PATH_MAX));

   gchar * tmp = g_strconcat(split, interface, NULL);
   if (tmp == NULL) {
      g_free(split);
      remove_devices();
      return 0;
   }

   gchar * buf = read_data_in_file(tmp);
   if (buf == NULL) {
      g_free(tmp);
      g_free(split);
      remove_devices();
      return 0;
   }

   gint bInterface = atoi(buf);

   g_free(buf);
   g_free(split);
   g_free(tmp);

   return bInterface;
}

static void mdev_device_add_interface(struct device_info * info)
{
   /* bInterfaceClass/bInterfaceSubClass/bInterfaceProtocol */
   gchar tmp[DEVICES_PATH_MAX];

   gint bInterfaceClass = mdev_device_get_interface("/bInterfaceClass", info);
   gint bInterfaceSubClass = mdev_device_get_interface("/bInterfaceSubClass", info);
   gint bInterfaceProtocol = mdev_device_get_interface("/bInterfaceProtocol", info);

   g_snprintf(tmp, DEVICES_PATH_MAX, "%01d/%01d/%01d", bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol);
   info->interface = g_strdup(tmp);
}

static gint mdev_device_add_number(struct device_info * info)
{
   gchar tmp[DEVICES_PATH_MAX];

   gint bInterfaceNumber = mdev_device_get_interface("/bInterfaceNumber", info);

   g_snprintf(tmp, DEVICES_PATH_MAX, "%02d", bInterfaceNumber);
   info->number = g_strdup(tmp);
}

static gint mdev_device_add_label(struct device_info * info)
{
   /* todo */
   if (info->label) {
      g_free(info->label);
   }

   info->label = NULL;
}

static gint mdev_device_add_sysattr(struct device_info * info)
{
   gchar * split = g_strndup(info->devpath, g_utf8_strlen(info->devpath, DEVICES_PATH_MAX) - g_utf8_strlen(info->devnode, DEVICES_PATH_MAX));
   gchar * tmp = g_strconcat(split, "/interface", NULL);
   gchar * buf = read_data_in_file(tmp);
   info->sysattr = g_strdup(buf);
   g_free(buf);
   g_free(tmp);
   g_free(split);
}

static void mdev_device_add_info_in_list(struct device_info * info)
{
   modem->devices = g_slist_insert_sorted(modem->devices, info,
         compare_device);
}

static gchar * mdev_device_get_subsystem(gchar * buffer)
{
   gint i;
   gchar ** tmp = g_strsplit(buffer, "/", -1);
   gchar * ret = NULL;

   for (i = 0; tmp[i] != NULL; i++) {
      if ( (strncmp(tmp[i], "tty", sizeof(char) * 3) == 0)
            ||(strncmp(tmp[i], "net", sizeof(char) * 3) == 0)
            ||(strncmp(tmp[i], "hsi", sizeof(char) * 3) == 0) ) {
         ret = g_strdup(tmp[i]);
         g_strfreev(tmp);
         return ret;
      }
   }

   g_strfreev(tmp);
   return ret;
}

static gboolean mdev_event_on_usb(GIOChannel *channel, GIOCondition cond, gpointer user_data)
{
   if (cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
      ofono_warn("Error with mdev monitor channel");
      return FALSE;
   }

   char buffer[1024];

   recv((int)(user_data), buffer, 1024, 0);

   gchar * subsystem = mdev_device_get_subsystem(buffer);
   if (subsystem == NULL) {
      return TRUE;
   }

   gchar ** tmp = g_strsplit(buffer, "@", 3);

   if (g_str_equal(tmp[0], "add") == TRUE) {
      if (check_if_device_is_already_unlogged(tmp[1])) {
        g_timeout_add_seconds(3, try_to_create_modem, NULL);
      }
   } else if (g_str_equal(tmp[0], "remove") == TRUE) {
      if (check_if_device_is_already_logged(tmp[1])) {
         remove_devices();
      }
   }

   g_strfreev(tmp);
   g_free(subsystem);

   return TRUE;
}

static int mdev_device_get_socket(void)
{
   int fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
   if(fd < 0) {
      DBG("error socket\n");
   } else {

      struct sockaddr_nl address;
      address.nl_family = AF_NETLINK;
      address.nl_pad = 0;
      address.nl_pid = 0;
      address.nl_groups = 1;

      int ret = bind(fd, (struct sockaddr *)(&address), sizeof(address));
      if(ret) {
         DBG("error bind\n");
      }
   }
   return fd;
}

static void get_modem_info(void)
{
   DBG("");
   gint flags = 0;

   if (nftw(DEVICES_PATH, find_modem_info_file, 20, (flags |= FTW_PHYS)) == -1) {
      modem_info_delete();
   }

   if (modem && modem->vendor)
      mdev_device_add_driver();
}

static gboolean get_device_info(void)
{
   DBG("");
   gint flags = 0;

   if (nftw((const char *)modem->syspath, find_device_info_file, 20, (flags |= FTW_PHYS)) == -1) {
      device_info_delete();
      return FALSE;
   }

   return TRUE;
}

static gchar * read_data_in_file(const gchar * path)
{
   if (path != NULL) {
      gssize length;
      gchar * content = NULL;
      g_file_get_contents (path, &content, &length, NULL);

      return content;
   }
   return NULL;
}

static void device_info_delete(void)
{
   if (modem != NULL) {
      if (modem->modem != NULL) {
         ofono_modem_remove(modem->modem);
         modem->modem = NULL;
      }

      if (modem->devices && modem->devices->data != NULL) {
         GSList *list;
         for (list = modem->devices; list; list = list->next) {
            struct device_info *info = list->data;

            DBG("%s", info->devnode);

            g_free(info->devpath);
            g_free(info->devnode);
            g_free(info->interface);
            g_free(info->number);
            g_free(info->label);
            g_free(info->sysattr);
            g_free(info);

            list->data = NULL;
         }

         g_slist_free(modem->devices);
      }
   }
}

static void modem_info_delete(void)
{
   DBG("");
   if (modem != NULL) {
      if (modem->vendor != NULL) {
         g_free(modem->vendor);
         modem->vendor = NULL;
      }

      if (modem->model != NULL) {
         g_free(modem->model);
         modem->model = NULL;
      }

      if (modem->syspath != NULL) {
         g_free(modem->syspath);
         modem->syspath = NULL;
      }

      if (modem->devname != NULL) {
         g_free(modem->devname);
         modem->devname = NULL;
      }

      if (modem->driver != NULL) {
         g_free(modem->driver);
         modem->driver = NULL;
      }

      g_free(modem);
      modem = NULL;
   }
}

static gboolean check_if_device_is_already_logged(char * devpath)
{
   gboolean ret = FALSE;
   gchar * tmp = g_strconcat("/sys", devpath, NULL);

   if (modem != NULL) {
      GSList *list;
      for (list = modem->devices; list; list = list->next) {
         struct device_info * info = list->data;
         if (g_str_equal(tmp, info->devpath)) {
            ret = TRUE;
         }
      }
   }
   g_free(tmp);
   return ret;
}

static gboolean check_if_device_is_already_unlogged(char * devpath)
{
   gboolean ret = FALSE;
   gchar * tmp = g_strconcat("/sys", devpath, NULL);

   if (modem == NULL) {
      ret = TRUE;
   } else {
      GSList *list;
      for (list = modem->devices; list; list = list->next) {
         struct device_info * info = list->data;
         if (g_str_equal(tmp, info->devpath)) {
            ret = FALSE;
         }
      }
   }

   g_free(tmp);
   return ret;
}

static gboolean try_to_create_modem(gpointer data)
{
   if (modem == NULL)
      if (!add_devices()) {
         DBG("Unable to create devices.");
         return TRUE;
      }

   gboolean ret = create_modem(modem->syspath, NULL, NULL);
   return !ret;
}

static int remove_devices(void)
{
   DBG("");

   device_info_delete();
   modem_info_delete();
}

static int add_devices(void)
{
   DBG("");

   modem = g_try_new0(struct modem_info, 1);

   get_modem_info();
   if (modem && modem->vendor) {
     if(!get_device_info()) {
       modem_info_delete();
       return 0;
     }
   } else {
      modem_info_delete();
      return 0;
   }

   if (modem == NULL) {
      DBG("Modem is null");
      return 0;
   }

   if (modem->syspath == NULL) {
      DBG("Syspath is null.");
      return 0;
   }

   DBG("Vendor:\t%s", modem->vendor);
   DBG("Model:\t%s", modem->model);
   DBG("Syspath:\t%s\n", modem->syspath);
   DBG("Devname:\t%s\n", modem->devname);
   DBG("Driver:\t%s\n\n", modem->driver);

   GSList *list;
   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;
      DBG("Devnode:%s\n", info->devnode);
      DBG("Devpath:%s\n", info->devpath);
      DBG("Interface:%s\n", info->interface);
      DBG("Number:%s\n", info->number);
      DBG("Label:%s\n", info->label);
      DBG("Sysattr:%s\n", info->sysattr);
   }

   if ((modem->vendor == NULL) || (modem->model == NULL) || (modem->syspath == NULL))
   {
      modem_info_delete();
      return 0;
   }

   return 1;
}

static int find_modem_info_file(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
   if (strncmp((fpath + ftwbuf->base), VENDOR_ID, VPID_FILE_NAME_SIZE) == 0) {
      gchar * modealias = read_data_in_file(fpath);
      if (mdev_device_add_vendor_id(modealias) > 0)
      {
         mdev_device_add_syspath_id(fpath);
         mdev_device_add_devname(modem->syspath);
      } else {
         g_free(modealias);
      }
   }

   if (strncmp((fpath + ftwbuf->base), PRODUCT_ID, VPID_FILE_NAME_SIZE) == 0) {
      gchar * modealias = read_data_in_file(fpath);
      if (mdev_device_add_model_id(modealias) == 0) {
         g_free(modealias);
      }
   }

   return 0;
}

static int find_device_info_file(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
   if (strncmp((fpath + ftwbuf->base), TTY_ACM, TTY_ACM_SIZE) == 0) {
      struct device_info *info;
      info = g_try_new0(struct device_info, 1);
      if (info == NULL)
         return -1;

      mdev_device_add_devnode((fpath + ftwbuf->base), info);
      mdev_device_add_devpath(fpath, info);
      mdev_device_add_interface(info);
      mdev_device_add_number(info);
      mdev_device_add_label(info);
      mdev_device_add_sysattr(info);

      mdev_device_add_info_in_list(info);
   }

   return 0;
}

static gboolean create_modem(gpointer key, gpointer value, gpointer user_data)
{
   const char *syspath = key;
   unsigned int i;

   if (modem->modem != NULL)
      return FALSE;

   DBG("%s", syspath);

   if (modem->devices == NULL)
      return FALSE;

   DBG("driver=%s", modem->driver);

   GSList * list;
   for (list = modem->devices; list; list = list->next) {
      struct device_info *info = list->data;

      gboolean test = g_file_test (info->devnode, G_FILE_TEST_EXISTS);

      if (!test)
        return FALSE;

      DBG("check devnode=%s", info->devnode);
   }

   modem->modem = ofono_modem_create(NULL, modem->driver);
   if (modem->modem == NULL)
      return FALSE;

   for (i = 0; driver_list[i].name; i++) {
      if (g_str_equal(driver_list[i].name, modem->driver) == FALSE)
         continue;

      if (driver_list[i].setup(modem) == TRUE) {
         ofono_modem_register(modem->modem);
         return FALSE;
      }
   }

   return TRUE;
}

static void start_usb_detection(void)
{
   DBG("");

   GIOChannel *channel;
   int fd = mdev_device_get_socket();
   if (fd < 0)
      return;

   channel = g_io_channel_unix_new(fd);
   if (channel == NULL)
      return;

   g_io_add_watch(channel,
         G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
         mdev_event_on_usb, (void *)fd);

   g_io_channel_unref(channel);
}

static int detect_init(void)
{
   DBG("");

   start_usb_detection();

   if (add_devices() == 1)
      create_modem(modem->syspath, NULL, NULL);

   return 0;
}

static void detect_exit(void)
{
   DBG("");
   remove_devices();
}


OFONO_PLUGIN_DEFINE(mdev, "mdev hardware detection", VERSION, OFONO_PLUGIN_PRIORITY_DEFAULT, detect_init, detect_exit)
