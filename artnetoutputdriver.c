/*
 * Copyright (C) 2012 Marco Gittler <g.marco@freenet.de>
 *
 * This file is part of DFAtmo the driver for 'Atmolight' controllers for XBMC and xinelib based video players.
 *
 * DFAtmo is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * DFAtmo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * This is the DFAtmo native file output driver.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <net/if.h>
#include <netdb.h>
#include <ifaddrs.h>

#ifdef WIN32
#include <windows.h>
#define strerror_r(num, buf, size)	strerror_s(buf, size, num)
#else
#include <sys/time.h>
#endif

#include "dfatmo.h"

typedef struct {
  output_driver_t output_driver;
  atmo_parameters_t param;
  int start_chan[9];
  uint8_t seq;
	uint8_t artnet_header[18];
	uint8_t artnet_data[512];
	char host[255];
  int id;
} artnet_output_driver_t;


int bcast_addr(char* str,int size)
{
  struct ifaddrs *ifaddr, *ifa;
  int family, s,ret=0;
  char host[NI_MAXHOST];

  if (getifaddrs(&ifaddr) == -1) {
    perror("getifaddrs");
    return ret;
  }

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL)
      continue;

    family = ifa->ifa_addr->sa_family;

    if (family == AF_INET || family == AF_INET6) {
      s=getnameinfo(ifa->ifa_broadaddr,
          (family == AF_INET) ? sizeof(struct sockaddr_in) :
          sizeof(struct sockaddr_in6),
          host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
      // if if is up and hast broadcast addr and is not localhost
      if (s==0  && ifa->ifa_flags& (IFF_BROADCAST|IFF_UP)  && ! (ifa->ifa_flags & IFF_LOOPBACK ) ){   
        if (str) {
          strncpy(str,host,size);
          ret=1;
        }
      }
    }
  }

  freeifaddrs(ifaddr);
  return ret;
}

#define PORT 6454
/* diep(), #includes and #defines like in the server */
void send_artnet(artnet_output_driver_t * this){

  struct sockaddr_in si_other;
  int s, slen=sizeof(si_other);
	memcpy(this->artnet_header,"Art-Net\x0\x0\x50\x0\x0e\x0\x0\x0\x0\x2\x0",18);
	this->artnet_header[12]=this->seq++;
  if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
    printf("socket");
  memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
  si_other.sin_port = htons(PORT);
	//DFATMO_LOG(DFLOG_ERROR,"sending to %s",this->host);
  si_other.sin_addr.s_addr = inet_addr(this->host);
  int broadcast=1;
	if (setsockopt(s,SOL_SOCKET,SO_BROADCAST, &broadcast,sizeof(broadcast))==-1) {
		printf("%s",strerror(errno));
	}
	int ret=sendto(s,this->artnet_header , sizeof(this->artnet_header)+sizeof(this->artnet_data), 0, (struct sockaddr *)&si_other, slen);
	if (ret==-1)
		printf("sendto() %d",ret);

	close(s);
}


static int output_rgb(artnet_output_driver_t *this,int chan,int r,int g,int b){
  int nBytes=0,i;
  for (i=0;i<6;i++){
    //DFATMO_LOG(DFLOG_ERROR, "sending USB control transfer message %d %d %d %d",chan,r,g,b);
		this->artnet_data[chan]=r;
		this->artnet_data[chan+1]=g;
		this->artnet_data[chan+2]=b;
		this->artnet_data[chan+3]=0;
		this->artnet_data[chan+4]=0;
		this->artnet_data[chan+5]=0;
  }
  return nBytes;
}

static void clear_all(artnet_output_driver_t *this){

  int i=0;
	for (i=0;i<9;i++){
    output_rgb(this,this->start_chan[i],0,0,0);
	}
	send_artnet(this);
}

static int artnet_driver_open(output_driver_t *this_gen, atmo_parameters_t *p) {
  artnet_output_driver_t *this = (artnet_output_driver_t *) this_gen;

  this->param = *p;
  this->id = 0;
	memset(this->host,0,255);
  if (this->param.driver_param && strlen(this->param.driver_param)){
    char *result=NULL;
		char *var_ptr=NULL,*value_ptr=NULL;
    int i=0,j;

    // parse driver_params, start addr of dmx for top,bottom,left,right,center,top_left,top_right,bottom_left,bottom_right
    for (j=0;j<9;j++){
      this->start_chan[j]=0;
    }

		// split first by ; use artnet_ip=1.2.3.4;channels=1,2,3,4,5,6 or only channels
		result=strtok_r(this->param.driver_param,";",&var_ptr);
		while (result!=NULL){
				char* var_name=strtok_r(result,"=",&value_ptr);
				char* var_value=strtok_r(NULL,"=",&value_ptr);

				if (strcmp(var_name,"artnet_ip")==0) {
						strncpy(this->host,var_value,255);
				} else if (strcmp(var_name,"channels")==0){
						char * chan_ptr=NULL;
						char * chan_result= strtok_r(var_value,",",&chan_ptr);
						while (chan_result!=NULL && i<9){
							this->start_chan[i]=atoi(chan_result);
							chan_result=strtok_r(NULL,",",&chan_ptr);
							i++;
						}

				} else {
						fprintf(stderr,"unknown variable %s\n",var_name);
				}
				result=strtok_r(NULL,";",&var_ptr);
		}
  }

	if (strlen(this->host)==0 && ! bcast_addr(this->host,255)){
					strcpy(this->host,"127.0.0.1");
	} 
	clear_all(this);
	return 0;
}

static int artnet_driver_configure(output_driver_t *this_gen, atmo_parameters_t *param) {
  artnet_output_driver_t *this = (artnet_output_driver_t *) this_gen;
  param->top = this->param.top;
  param->bottom = this->param.bottom;
  param->left = this->param.left;
  param->right = this->param.right;
  param->center = this->param.center;
  param->top_left = this->param.top_left;
  param->top_right = this->param.top_right;
  param->bottom_left = this->param.bottom_left;
  param->bottom_right = this->param.bottom_right;

  this->param = *param;
	clear_all(this);
  return 0;
}

static int artnet_driver_close(output_driver_t *this_gen) {
  artnet_output_driver_t *this = (artnet_output_driver_t *) this_gen;

  clear_all(this);
  return 0;
}

static void artnet_driver_dispose(output_driver_t *this_gen) {
  free(this_gen);
}


static int artnet_driver_output_colors(output_driver_t *this_gen, rgb_color_t *colors, rgb_color_t *last_colors) {
  artnet_output_driver_t *this = (artnet_output_driver_t *) this_gen;
  int c, secs, msecs;


#ifdef WIN32
  {
    SYSTEMTIME t;
    GetSystemTime(&t);
    secs = (int)t.wSecond;
    msecs = (int)t.wMilliseconds;
  }
#else
  {
    struct timeval tvnow;
    gettimeofday(&tvnow, NULL);
    secs = (int)(tvnow.tv_sec % 60);
    msecs = (int)(tvnow.tv_usec / 1000);
  }
#endif
  for (c = 1; c <= this->param.top; ++c, ++colors){
    if (c==1) output_rgb(this,this->start_chan[0],colors->r,colors->g,colors->b);
  }

  for (c = 1; c <= this->param.bottom; ++c, ++colors){
    if (c==1) output_rgb(this,this->start_chan[1],colors->r,colors->g,colors->b);
	}

  for (c = 1; c <= this->param.left; ++c, ++colors){
    if (c==1) output_rgb(this,this->start_chan[2],colors->r,colors->g,colors->b);
  }

  for (c = 1; c <= this->param.right; ++c, ++colors){
    if (c==1) output_rgb(this,this->start_chan[3],colors->r,colors->g,colors->b);
  }

  if (this->param.center) {
    output_rgb(this,this->start_chan[4],colors->r,colors->g,colors->b);
    ++colors;
  }

  if (this->param.top_left) {
    output_rgb(this,this->start_chan[5],colors->r,colors->g,colors->b);
    ++colors;
  }
  if (this->param.top_right) {
    output_rgb(this,this->start_chan[6],colors->r,colors->g,colors->b);
    ++colors;
  }
		
  if (this->param.bottom_left) {
    output_rgb(this,this->start_chan[7],colors->r,colors->g,colors->b);
    ++colors;
  }
  if (this->param.bottom_right) {
    output_rgb(this,this->start_chan[8],colors->r,colors->g,colors->b);
  }
  /*if (ferror(fd)) {
    strerror_r(errno, this->output_driver.errmsg, sizeof(this->output_driver.errmsg));
    return -1;
  }*/
	send_artnet(this);
  return 0;
}


dfatmo_log_level_t dfatmo_log_level;
dfatmo_log_t dfatmo_log;

output_driver_t* dfatmo_new_output_driver(dfatmo_log_level_t log_level, dfatmo_log_t log_fn) {
  artnet_output_driver_t *d;

  if (dfatmo_log_level == NULL) {
    dfatmo_log_level = log_level;
    dfatmo_log = log_fn;
  }

  d = (artnet_output_driver_t *) calloc(1, sizeof(artnet_output_driver_t));
  if (d == NULL)
    return NULL;

  d->output_driver.version = DFATMO_OUTPUT_DRIVER_VERSION;
  d->output_driver.open = artnet_driver_open;
  d->output_driver.configure = artnet_driver_configure;
  d->output_driver.close = artnet_driver_close;
  d->output_driver.dispose = artnet_driver_dispose;
  d->output_driver.output_colors = artnet_driver_output_colors;
  return &d->output_driver;
}
