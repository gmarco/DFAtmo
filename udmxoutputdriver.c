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
 * This is the DFAtmo udmx output driver.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <libusb.h>

#ifdef WIN32
#include <windows.h>
#define strerror_r(num, buf, size)     strerror_s(buf, size, num)
#else
#include <sys/time.h>
#endif

#include "dfatmo.h"

#define USBDEV_SHARED_VENDOR    0x16C0  /* VOTI */
#define USBDEV_SHARED_PRODUCT   0x05DC  /* Obdev's*/

typedef struct {
  output_driver_t output_driver;
  atmo_parameters_t param;
  int start_chan[9];
  libusb_device_handle      *handle;
  int id;
} file_output_driver_t;


static libusb_device_handle   *findDevice(void)
{
        struct libusb_device_handle *handle=NULL;
        int r=0;
        r=libusb_init(NULL);
        if (r){
          fprintf(stderr, "warning: error init usb: %s\n", libusb_error_name(r));
          return NULL;
        }
        libusb_device **devs;



        ssize_t cnt=libusb_get_device_list(NULL, &devs);
        if (cnt<0)
          return NULL;
        libusb_device *dev;
        int i=0;

        while ((dev = devs[i++]) != NULL) {
          struct libusb_device_descriptor desc;
          int r = libusb_get_device_descriptor(dev, &desc);
                if (r){
                  fprintf(stderr, "warning: error reading usb desc: %s\n", libusb_error_name(r));
                  return NULL;
                }
            if(desc.idVendor == USBDEV_SHARED_VENDOR && desc.idProduct == USBDEV_SHARED_PRODUCT){
                unsigned char    string[256];
                int     len;
                handle=libusb_open_device_with_vid_pid(NULL, desc.idVendor,desc.idProduct);
                if(!handle){
                    fprintf(stderr, "Warning: cannot open USB device: %s\n", "test");
                    continue;
                }
                // now find out whether the device actually is obdev's Remote Sensor: 
                len = libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, string, sizeof(string));
                if(len < 0){
                    fprintf(stderr, "warning: cannot query manufacturer for device: %s\n", libusb_error_name (len) );
                    goto skipDevice;
                }
                if(strcmp((const char*)string, (const char*)"www.anyma.ch") != 0)
                    goto skipDevice;
                len = libusb_get_string_descriptor_ascii(handle, desc.iProduct, string, sizeof(string));
                if(len < 0){
                    fprintf(stderr, "warning: cannot query product for device: %s\n", libusb_error_name(len));
                    goto skipDevice;
                }
                if(strcmp((const char*)string, "uDMX") == 0)
                    break;
skipDevice:
                libusb_release_interface(handle, 0);
                handle = NULL;
            }
        if(handle)
            break;
    }
    if(!handle)
        fprintf(stderr, "Could not find USB device www.anyma.ch/uDMX\n");
        libusb_free_device_list(devs, 1);
    return handle;
}

static int output_rgb(libusb_device_handle *handle,int chan,int r,int g,int b){
  int nBytes=0,i;
  unsigned char      buffer[8];
  int colors[6]={r,g,b,0,0,0};
  for (i=0;i<6;i++){
     //DFATMO_LOG(DFLOG_ERROR, "%s: sending USB control transfer message %d %d %d %d",chan,r,g,b);
     nBytes += libusb_control_transfer(handle, LIBUSB_RECIPIENT_DEVICE |LIBUSB_REQUEST_TYPE_VENDOR|LIBUSB_ENDPOINT_OUT,
        0x01, colors[i], chan+i, buffer, sizeof(buffer), 5000);
  }
  return nBytes;
}

static void clear_all(output_driver_t *this_gen){

  file_output_driver_t *this = (file_output_driver_t *) this_gen;
  int i=0;
for (i=0;i<9;i++)
          output_rgb(this->handle,this->start_chan[i],0,0,0);
}
static int file_driver_open(output_driver_t *this_gen, atmo_parameters_t *p) {
  file_output_driver_t *this = (file_output_driver_t *) this_gen;

  this->param = *p;
  this->id = 0;

  if (this->param.driver_param && strlen(this->param.driver_param)){
    // parse driver_params, start addr of dmx for top,bottom,left,right,center,top_left,top_right,bottom_left,bottom_right
    char *result=NULL;
    char delims[]=",;-";
    int i=0,j;
    for (j=0;j<9;j++){
      this->start_chan[j]=0;
    }
    result= strtok(this->param.driver_param,delims);
    while (result!=NULL && i<9){
      this->start_chan[i]=atoi(result);
      result=strtok(NULL,delims);
      i++;
    }
  }

  this->handle=findDevice();

  if (this->handle == NULL) {
    strerror_r(errno, this->output_driver.errmsg, sizeof(this->output_driver.errmsg));
    return -1;
  }
clear_all(this_gen);
  return 0;
}

static int file_driver_configure(output_driver_t *this_gen, atmo_parameters_t *param) {
  file_output_driver_t *this = (file_output_driver_t *) this_gen;
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
clear_all(this_gen);
  return 0;
}

static int file_driver_close(output_driver_t *this_gen) {
  file_output_driver_t *this = (file_output_driver_t *) this_gen;

clear_all(this_gen);
  if (this->handle) {
    libusb_release_interface(this->handle, 0);
    libusb_exit(NULL);
    this->handle = NULL;
  }
  return 0;
}

static void file_driver_dispose(output_driver_t *this_gen) {
  free(this_gen);
}


static int file_driver_output_colors(output_driver_t *this_gen, rgb_color_t *colors, rgb_color_t *last_colors) {
  file_output_driver_t *this = (file_output_driver_t *) this_gen;
  libusb_device_handle *handle = this->handle;
  int c, secs, msecs;

  if (handle == NULL){
    return -1;
    }

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
  //fprintf(fd, "%d: %02d.%03d ---\n", this->id++, secs, msecs);
  for (c = 1; c <= this->param.top; ++c, ++colors){
    //fprintf(stderr,"     top %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);
    if (c==1) output_rgb(handle,this->start_chan[0],colors->r,colors->g,colors->b);
    }

  for (c = 1; c <= this->param.bottom; ++c, ++colors){
    //fprintf(stderr,"     bottom %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);
    if (c==1) output_rgb(handle,this->start_chan[1],colors->r,colors->g,colors->b);
    }

  for (c = 1; c <= this->param.left; ++c, ++colors){
    //fprintf(stderr,"     left %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);
    if (c==1) output_rgb(handle,this->start_chan[2],colors->r,colors->g,colors->b);
    }
  for (c = 1; c <= this->param.right; ++c, ++colors){
    //fprintf(stderr,"     right %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);
    if (c==1) output_rgb(handle,this->start_chan[3],colors->r,colors->g,colors->b);
  }
  if (this->param.center) {
    //fprintf(stderr,"     center %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);
    output_rgb(handle,this->start_chan[4],colors->r,colors->g,colors->b);
    ++colors;
  }
  if (this->param.top_left) {
    //fprintf(stderr,"     tl %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);
    output_rgb(handle,this->start_chan[5],colors->r,colors->g,colors->b);
    ++colors;
  }
  if (this->param.top_right) {
    //fprintf(stderr,"     tr %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);
    output_rgb(handle,this->start_chan[6],colors->r,colors->g,colors->b);
    ++colors;
  }
  if (this->param.bottom_left) {
    //fprintf(stderr,"     bl %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);
    output_rgb(handle,this->start_chan[7],colors->r,colors->g,colors->b);
    ++colors;
  }
  if (this->param.bottom_right) {
    //fprintf(stderr,"     br %2d: %3d %3d %3d\n", c, colors->r, colors->g, colors->b);
    output_rgb(handle,this->start_chan[8],colors->r,colors->g,colors->b);
  }
  /*if (ferror(fd)) {
    strerror_r(errno, this->output_driver.errmsg, sizeof(this->output_driver.errmsg));
    return -1;
  }*/
  return 0;
}


dfatmo_log_level_t dfatmo_log_level;
dfatmo_log_t dfatmo_log;

output_driver_t* dfatmo_new_output_driver(dfatmo_log_level_t log_level, dfatmo_log_t log_fn) {
  file_output_driver_t *d;

  if (dfatmo_log_level == NULL) {
    dfatmo_log_level = log_level;
    dfatmo_log = log_fn;
  }

  d = (file_output_driver_t *) calloc(1, sizeof(file_output_driver_t));
  if (d == NULL)
    return NULL;

  d->output_driver.version = DFATMO_OUTPUT_DRIVER_VERSION;
  d->output_driver.open = file_driver_open;
  d->output_driver.configure = file_driver_configure;
  d->output_driver.close = file_driver_close;
  d->output_driver.dispose = file_driver_dispose;
  d->output_driver.output_colors = file_driver_output_colors;
  return &d->output_driver;
}
