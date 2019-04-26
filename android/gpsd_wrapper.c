#include <stdio.h>
#include <stdlib.h>
#include <android/log.h>
#include <cutils/properties.h>
#include <sys/system_properties.h>

int main (){
	char gpsd_params[PROP_VALUE_MAX];
	char cmd[1024];
	int i = 0;
	property_get("service.gpsd.parameters", gpsd_params, "-Nn,-D2,/dev/ttyACM0,/dev/ttyACM1");
	while (gpsd_params[i] != 0){
		if (gpsd_params[i] == ',') gpsd_params[i] = ' ';
		i++;
	}

	sprintf(cmd, "/vendor/bin/logwrapper /vendor/bin/gpsd %s", gpsd_params);

	__android_log_print(ANDROID_LOG_DEBUG, "gpsd_wrapper", "Starting gpsd: %s", cmd);

	system(cmd);
	return 0;
}

