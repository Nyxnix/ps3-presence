#include "screen.h"
#include "font8x8_basic.h"
static uint32_t *pixels; static unsigned width,height,scale,origin;
static void rect(unsigned x,unsigned y,unsigned w,unsigned h,uint32_t color) {
    x=x*scale+origin; y*=scale; w*=scale; h*=scale;
    for(unsigned row=y;row<y+h && row<height;row++)
        for(unsigned col=x;col<x+w && col<width;col++) pixels[row*width+col]=color;
}
static void text(unsigned x,unsigned y,const char *s,unsigned size,uint32_t color) {
    unsigned initial=x;
    for(;*s;s++) {
        if(*s=='\n') { x=initial; y+=12*size; continue; }
        unsigned ch=(unsigned char)*s; if(ch>=128) ch='?';
        for(unsigned r=0;r<8;r++) for(unsigned c=0;c<8;c++)
            if(font8x8_basic[ch][r]&(1u<<c)) rect(x+c*size,y+r*size,size,size,color);
        x+=8*size;
    }
}
void screen_render(uint32_t *p,unsigned w,unsigned h,unsigned selected,int token_set,int enabled,const char *message,int installer) {
    pixels=p; width=w; height=h; scale=h/360; if(!scale) scale=1;
    origin=w>640*scale?(w-640*scale)/2:0;
    for(unsigned i=0;i<w*h;i++) p[i]=0x101118;
    text(40,32,"PS3 Presence",3,0xf5f3fa);
    text(42,70,installer==APP_CONFIRM_REMOVE?"UNINSTALL":installer==APP_REMOVED?"REMOVED":(installer==APP_INSTALLER || installer==APP_RESTARTING)?"INSTALLER":"CONFIGURATION",1,0xa99ab9);
    if(installer==APP_CONFIRM_REMOVE) {
        text(42,112,"Remove PS3 Presence?",2,0xf5f3fa);
        text(42,153,"Deletes the app, plugin, startup entries,",1,0xd2c9dd);
        text(42,171,"saved token, settings and diagnostic files.",1,0xd2c9dd);
        text(42,204,"Other apps and plugins are kept.",1,0xd2c9dd);
    } else if(installer==APP_REMOVED) {
        text(42,124,"Uninstall complete",2,0x8bd6a1);
        text(42,169,"The plugin has stopped and its files are removed.",1,0xd2c9dd);
    } else if(installer==APP_RESTARTING) {
        text(42,124,"Installation complete",2,0x8bd6a1);
        text(42,169,"Your PS3 will restart to activate this build.",1,0xd2c9dd);
    } else {
        for(unsigned row=0;row<3;row++) rect(40,98+row*52,560,43,selected==row?0x443153:0x21212b);
        rect(40,98+selected*52,4,43,0xb28cd9);
        text(58,112,"Token:",2,0xf5f3fa);
        text(220,112,token_set?"********  Edit":"Not set   Add",2,0xccc1da);
        text(58,164,"Presence:",2,0xf5f3fa);
        text(420,164,enabled?"ON":"OFF",2,enabled?0x8bd6a1:0xb8b3c1);
        text(58,216,"Uninstall",2,0xf0b1ba);
    }
    if(message && *message) {
        /* Wrap messages at whole words into the footer, without leaking tokens. */
        char line[68]; unsigned at=0,y=254;
        while(*message && y<292) {
            while(message[at] && at<66) at++;
            unsigned n=at;
            if(message[n]) { while(n && message[n]!=' ') n--; if(!n) n=at; }
            for(unsigned i=0;i<n;i++) line[i]=message[i]; line[n]=0;
            text(42,y,line,1,0xd2c9dd); message+=n; while(*message==' ') message++;
            at=0; y+=12;
        }
    }
    if(installer!=APP_RESTARTING)
        text(42,310,installer==APP_CONFIRM_REMOVE?"X Uninstall    O Cancel":installer==APP_REMOVED?"O Return to XMB":"UP/DOWN Select    X Choose    O Exit",1,0xe4dceb);
    if(installer==APP_SETTINGS || installer==APP_INSTALLER)
        text(42,331,"Hold L1 when launching to install or update",1,0x9d94a9);
}
