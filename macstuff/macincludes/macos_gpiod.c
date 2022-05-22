//
//  macos_gpiod.c
//  coopserver
//
//  Created by Vincent Moscaritolo on 12/24/21.
//

#include <stdio.h>
#include <errno.h>
#include <time.h>
#if defined(__APPLE__)
// used for cross compile on osx

#include "macos_gpiod.h"

struct gpiod_chip {
	struct gpiod_line **lines;
	unsigned int num_lines;

	int fd;

	char name[32];
	char label[32];
};

struct gpiod_line {
	unsigned int offset;

	/* The direction of the GPIO line. */
	int direction;

	/* The active-state configuration. */
	int active_state;

	/* The logical value last written to the line. */
	int output_value;

	/* The GPIOLINE_FLAGs returned by GPIO_GET_LINEINFO_IOCTL. */
	uint32_t info_flags;

	/* The GPIOD_LINE_REQUEST_FLAGs provided to request the line. */
	uint32_t req_flags;

	/*
	 * Indicator of LINE_FREE, LINE_REQUESTED_VALUES or
	 * LINE_REQUESTED_EVENTS.
	 */
	int state;

	struct gpiod_chip *chip;
	struct line_fd_handle *fd_handle;

	char name[32];
	char consumer[32];
};


struct gpiod_chip *gpiod_chip_open(const char *path) {
	
	printf("gpiod_chip_open(%s)\n",path );
	 
	struct gpiod_chip * chip = malloc(sizeof (struct gpiod_chip));
  
	return  chip;
};


void gpiod_chip_close(struct gpiod_chip *chip){
	if(chip){
		printf("gpiod_chip_close\n" );
		free(chip);
	}
}

int gpiod_chip_get_lines(struct gpiod_chip *chip,
			 unsigned int *offsets, unsigned int num_offsets,
			 struct gpiod_line_bulk *bulk){
	
	bulk->num_lines = num_offsets;
	
	printf("gpiod_chip_get_lines\n" );
	return 0;
}

void gpiod_line_release_bulk(struct gpiod_line_bulk *bulk) {
	 
	printf("gpiod_line_release_bulk\n" );
}
 
int gpiod_line_request_bulk(struct gpiod_line_bulk *bulk,
				 const struct gpiod_line_request_config *config,
				 const int *default_vals){
	
	printf("gpiod_line_request_bulk\n" );
	return 0;
}
 

int gpiod_line_get_value_bulk(struct gpiod_line_bulk *bulk, int *values){
	
//	printf("gpiod_line_get_value_bulk\n" );

	for(int i = 0; i < bulk->num_lines; i++){
		values[i] = 0;
	}
	return 0;
}


int gpiod_line_set_value_bulk(struct gpiod_line_bulk *bulk, const int *values) {
	
	printf("gpiod_line_set_value_bulk( " );

 	for(int i = 0; i < bulk->num_lines; i++){
		
		printf("%s ", values[i] == 0?"OFF":"ON");
 	}
 	printf(")\n");
	return 0;
}


//struct gpiod_line *
//gpiod_chip_get_line(struct gpiod_chip *chip, unsigned int offset) GPIOD_API;

struct gpiod_line *gpiod_chip_get_line(struct gpiod_chip *chip, unsigned int offset) {
	
	printf("gpiod_chip_get_line(%d)\n",offset);
	 
	struct gpiod_line * line = malloc(sizeof (struct gpiod_line));
  
	return  line;
};


void gpiod_line_release(struct gpiod_line *line){
	if(line){
		printf("gpiod_line_release\n" );
		free(line);
	}
}

int gpiod_line_request_falling_edge_events_flags(struct gpiod_line *line,
						 const char *consumer,
						 int flags)
{
	
	printf("gpiod_line_request_falling_edge_events_flags()\n");
 	return 0;
}


int gpiod_line_event_read(struct gpiod_line *line,
								  struct gpiod_line_event *event)  {
	printf("gpiod_line_event_read()\n");
	return 0;
}

int gpiod_line_event_wait(struct gpiod_line *line,
								  const struct timespec *timeout) {
	
	nanosleep(timeout, NULL);
	
	//	printf("gpiod_line_event_wait()\n");
	
	return 0;
}

#endif
