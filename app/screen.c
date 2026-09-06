#include "screen.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
static gcmContextData *context;
static void *host;
static uint32_t *buffers[2];
static uint32_t offsets[2],depth_offset;
static void *depth;
static unsigned width,height,current;
int screen_init(void) {
    videoState state; videoResolution resolution; videoConfiguration config;
    host=memalign(1024*1024,1024*1024); if(!host) return 0;
    if(rsxInit(&context,64*1024,1024*1024,host) || !context) return 0;
    if(videoGetState(0,0,&state) || state.state || videoGetResolution(state.displayMode.resolution,&resolution)) return 0;
    width=resolution.width; height=resolution.height;
    memset(&config,0,sizeof(config)); config.resolution=state.displayMode.resolution;
    config.format=VIDEO_BUFFER_FORMAT_XRGB; config.pitch=width*4; config.aspect=state.displayMode.aspect;
    if(videoConfigure(0,&config,0,0)) return 0;
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for(unsigned i=0;i<2;i++) {
        buffers[i]=rsxMemalign(64,width*height*4);
        if(!buffers[i] || rsxAddressToOffset(buffers[i],&offsets[i]) || gcmSetDisplayBuffer(i,offsets[i],width*4,width,height)) return 0;
        memset(buffers[i],0,width*height*4);
    }
    depth=rsxMemalign(64,width*height*4);
    if(!depth || rsxAddressToOffset(depth,&depth_offset)) return 0;
    gcmResetFlipStatus();
    if(gcmSetFlip(context,1)) return 0;
    rsxFlushBuffer(context); gcmSetWaitFlip(context); return 1;
}
void screen_draw(unsigned selected,int token_set,int enabled,const char *message,int installer) {
    while(gcmGetFlipStatus()) usleep(200);
    gcmResetFlipStatus();
    screen_render(buffers[current],width,height,selected,token_set,enabled,message,installer);
    /* Give system overlays (including OSK) a valid current render surface. */
    gcmSurface surface; memset(&surface,0,sizeof(surface));
    surface.colorFormat=GCM_SURFACE_X8R8G8B8; surface.colorTarget=GCM_SURFACE_TARGET_0;
    surface.colorLocation[0]=GCM_LOCATION_RSX; surface.colorOffset[0]=offsets[current]; surface.colorPitch[0]=width*4;
    for(unsigned i=1;i<4;i++) { surface.colorLocation[i]=GCM_LOCATION_RSX; surface.colorPitch[i]=64; }
    surface.depthFormat=GCM_SURFACE_ZETA_Z16; surface.depthLocation=GCM_LOCATION_RSX;
    surface.depthOffset=depth_offset; surface.depthPitch=width*4;
    surface.type=GCM_SURFACE_TYPE_LINEAR; surface.antiAlias=GCM_SURFACE_CENTER_1;
    surface.width=width; surface.height=height; rsxSetSurface(context,&surface);
}
void screen_flip(void) {
    if(!gcmSetFlip(context,current)) { rsxFlushBuffer(context); gcmSetWaitFlip(context); current^=1; }
}
void screen_end(void) {
    if(context) rsxFinish(context,1);
    for(unsigned i=0;i<2;i++) if(buffers[i]) rsxFree(buffers[i]);
    if(depth) rsxFree(depth);
    free(host);
}
