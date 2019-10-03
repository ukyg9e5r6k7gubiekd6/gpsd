//#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <sys/types.h>
#include "../json.h"

static int test_ver(void);
static int test_watch(void);
static int test_tpv(void);
int main(void);

static char *VER =
    "{\"class\":\"VERSION\",\"release\":\"3.19.1~dev\",\"rev\":\"release-3.19-"
    "655-gb4aded4c1\",\"proto_major\":3,\"proto_minor\":14}";
static char *WAT =
    "{\"class\":\"WATCH\",\"enable\":true,\"json\":true,\"nmea\":false,\"raw\":"
    "0,\"scaled\":false,\"timing\":false,\"split24\":false,\"pps\":false,"
    "\"device\":\"/dev/ttyUSB0\"}";
static char *TPV =
    "{\"class\":\"TPV\",\"device\":\"/dev/"
    "ttyUSB0\",\"mode\":3,\"time\":\"2019-10-04T08:51:34.000Z\",\"ept\":0.005,"
    "\"lat\":46.367303831,\"lon\":-116.963791235,\"altHAE\":460.834,\"altMSL\":"
    "476.140,\"epx\":7.842,\"epy\":12.231,\"epv\":30.607,\"track\":57.1020,"
    "\"magtrack\":70.9299,\"magvar\":13.8,\"speed\":0.065,\"climb\":-0.206,"
    "\"eps\":24.46,\"epc\":61.21,\"ecefx\":-1999242.00,\"ecefy\":-3929871.00,"
    "\"ecefz\":4593848.00,\"ecefvx\":0.12,\"ecefvy\":0.12,\"ecefvz\":-0.12,"
    "\"velN\":0.035,\"velE\":0.055,\"velD\":0.206,\"geoidSep\":-15.307,\"eph\":"
    "15.200,\"sep\":31.273}";

#define icmp(want, got)                                                        \
  if (want != got) {                                                           \
    tally++;                                                                   \
    printf("wanted %d got %d\n", want, got);                                   \
  }
#define fcmp(want, got, tol)                                                   \
  if (fabs(want - got) > tol) {                                                \
    tally++;                                                                   \
    printf("wanted %f got %f diff %f > %f\n", want, got, (want-got), tol);     \
  }
#define scmp(want, got)                                                        \
  if ((NULL == &got)||(0 != strcmp(want, got))) {                                 \
    tally++;                                                                   \
    printf("wanted '%s' got '%s'\n", want, got);                               \
  }
#define fin() icmp(0, errno) return tally;

static int test_ver() {
  int tally = 0;
  char revision[50];
  __uint16_t pvhi, pvlo;
  const struct json_attr_t json_attrs_version[] = {
      {"class", t_check, .dflt.check = "VERSION"},
      {"rev", t_string, .addr.string = (char *)&revision, .len = 50},
      {"proto_major", t_ushort, .addr.ushortint = &pvhi},
      {"proto_minor", t_ushort, .addr.ushortint = &pvlo},
      {"", t_ignore},
      {NULL},
  };
  printf(".");
  errno = json_read_object(VER, json_attrs_version, NULL);
  icmp(3, pvhi);
  icmp(14, pvlo);
  fin();
}

static int test_watch() {
  int tally = 0;
  bool enable, json;
  const struct json_attr_t json_attrs_watch[] = {
      {"class", t_check, .dflt.check = "WATCH"},
      {"device", t_check, .dflt.check = "/dev/ttyUSB0"},
      {"enable", t_boolean, .addr.boolean = &enable},
      {"json", t_boolean, .addr.boolean = &json},
      {"", t_ignore},
      {NULL},
  };
  printf(".");
  errno = json_read_object(WAT, json_attrs_watch, NULL);
  icmp(true, enable);
  icmp(true, json);
  fin();
}

static int test_tpv() {
  int tally = 0;
  int gps_mode;
  double ept;
  char gps_time[50];
  const struct json_attr_t json_attrs_tpv[] = {
      {"class", t_check, .dflt.check = "TPV"},
      {"device", t_check, .dflt.check = "/dev/ttyUSB0"},
      {"mode", t_integer, .addr.integer = &gps_mode, .dflt.integer = -1},
      {"time", t_string, .addr.string = (char *)&gps_time, .len = 50},
      {"ept", t_real, .addr.real = &ept, .dflt.real = NAN},
      {"", t_ignore},
      {NULL},
  };
  printf(".");
  errno = json_read_object(TPV, json_attrs_tpv, NULL);
  icmp(3, gps_mode) fcmp(0.005, ept, 0.001);
  scmp("2019-10-04T08:51:34.000Z", gps_time);
  fin();
}

int main() {
  int count = 0;
  count += test_ver();
  count += test_watch();
  count += test_tpv();
  if (count) {
	  printf("OOPS: %d\n", count);
  }
  return count;
}
