/* gpsmon.h -- what monitor capabuilities look like */

#define COMMAND_TERMINATE	-1
#define COMMAND_MATCH		1
#define COMMAND_UNKNOWN		0

struct mdevice_t {
    /* a device-specific capability table for the monitor */
    bool (*initialize)(void);		/* paint legends on windows */
    void (*analyze)(unsigned char [], size_t);
    void (*repaint)(bool);		/* now paint the data */
    int (*command)(char[]);		/* interpret device-specfic commands */
    void (*wrap)(void);			/* deallocate storage */
    int min_y, min_x;			/* space required for device info */
    const struct gps_type_t *driver;	/* device driver table */
};

// Device-specific code will need this.
extern bool monitor_control_send(unsigned char *buf, size_t len);

#define BUFLEN		2048

extern WINDOW *debugwin;
extern struct gps_context_t	context;
extern struct gps_device_t	session;
extern int gmt_offset;

/* gpsmon.h ends here */
