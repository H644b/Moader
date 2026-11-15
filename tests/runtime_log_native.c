/* Reuse owned SDK/lifetime mocks; invoke the production logger and producer. */
#define main unused_native_regression_main
#include "../runtime/modloader/native/verify-lazy.c"
#undef main
#include "../runtime/modloader/native/log-snapshot.h"
#include <errno.h>

static char snapshot_root[2048];
static int locked_slot=-1,fail_stage,report_read_failure;
static uint64_t snapshot_open_count,snapshot_close_count,snapshot_bytes;
static uint8_t *report_memory,*event_buffer;
static struct lfs_log_snapshot *snapshot_state;
static int slot_for(const char *path){return !strcmp(path,MOADER_LOG_PATH0)?0:!strcmp(path,MOADER_LOG_PATH1)?1:-1;}
static uint32_t snapshot_open(void *fs,struct ns_out *out,const char *path,uint32_t mode){
 int slot=slot_for(path);if(slot<0)return open_file(fs,out,path,mode);
 assert(!lazy.log_lock&&snapshot_state->publish_lock==1);
 assert(mode==3);*out->value=0;if(slot==locked_slot)return 0xe02;
 char name[2200];snprintf(name,sizeof(name),"%s/runtime/log%d.bin",snapshot_root,slot);
 FILE *file=fopen(name,"r+b");if(!file)return 0x202;
 struct object *o=object(1,0,MOADER_LOG_CAPACITY);o->reserved[0]=(uintptr_t)file;o->name=slot?MOADER_LOG_PATH1:MOADER_LOG_PATH0;
 *out->value=o;snapshot_open_count++;return 0;
}
static void snapshot_release(void *memory){
 struct object *o=memory;if(o->reserved[0]){assert(o->refs==1);assert(!fclose((FILE *)(uintptr_t)o->reserved[0]));snapshot_close_count++;}
 release(memory);
}
static uint32_t snapshot_size(void *file,uint64_t *size){
 struct object *o=file;if(!o->reserved[0])return file_size(file,size);
 assert(!lazy.log_lock&&snapshot_state->publish_lock==1);
 FILE *f=(FILE *)(uintptr_t)o->reserved[0];assert(!fseek(f,0,SEEK_END));long end=ftell(f);assert(end>=0);*size=(uint64_t)end;return 0;
}
static uint32_t snapshot_read(void *file,uint64_t *count,int64_t at,const struct ns_buffer *buffer,int64_t length,uint32_t option){
 if(file==context.log_file)assert(!lazy.log_lock&&snapshot_state->publish_lock==1);
 if(file==context.log_file&&report_read_failure)return 0x321;
 return file_read(file,count,at,buffer,length,option);
}
static uint32_t snapshot_write(void *file,int64_t at,const struct ns_buffer *buffer,int64_t length,uint32_t option){
 struct object *o=file;if(!o->reserved[0])return log_write(file,at,buffer,length,option);
 assert(!lazy.log_lock&&snapshot_state->publish_lock==1);
 assert(at>=0&&length>=0&&(uint64_t)length<=buffer->capacity&&(uint64_t)at+(uint64_t)length<=MOADER_LOG_CAPACITY);
 FILE *f=(FILE *)(uintptr_t)o->reserved[0];assert(!fseek(f,(long)at,SEEK_SET));
 if((fail_stage==1&&at==80)||(fail_stage==2&&at==0)){
  size_t n=length>3?3:(size_t)length;assert(fwrite(buffer->data,1,n,f)==n);return 0x456;
 }
 assert(fwrite(buffer->data,1,(size_t)length,f)==(size_t)length);snapshot_bytes+=(uint64_t)length;return 0;
}
static uint32_t snapshot_flush(void *file){
 struct object *o=file;if(!o->reserved[0])return log_flush(file);
 assert(!lazy.log_lock&&snapshot_state->publish_lock==1);
 assert(!fflush((FILE *)(uintptr_t)o->reserved[0]));return fail_stage==3?0x789:0;
}
void native_log_setup(const char *root){
 setup();assert(strlen(root)<sizeof(snapshot_root));strcpy(snapshot_root,root);
 locked_slot=-1;fail_stage=report_read_failure=0;snapshot_open_count=snapshot_close_count=snapshot_bytes=0;
 report_memory=calloc(1,1024*1024);event_buffer=calloc(1,0x4000);snapshot_state=calloc(1,sizeof(*snapshot_state));assert(report_memory&&event_buffer&&snapshot_state);
 /* Actual report length, deliberately not the backing allocation capacity. */
 const char header[]="Moader filesystem runtime\nSession: host\nActivation complete: 1\n";
 memcpy(report_memory,header,sizeof(header)-1);log_end=sizeof(header)-1;
 context.log_file=object(1,report_memory,1024*1024);context.write=snapshot_write;context.flush=snapshot_flush;context.open_file=snapshot_open;
 context.sdk.file_read=snapshot_read;context.sdk.file_size=snapshot_size;context.sdk.file_release=snapshot_release;
 lazy.log_cursor=log_end;lazy.log_limit=1024*1024;lazy.log_buffer=event_buffer;lazy.log_snapshot=snapshot_state;
 lfs_lazy_log_route(0,11,22);lfs_lazy_log_flush();assert(snapshot_state->sequence==1&&!snapshot_state->last_result);
}
void native_log_event(unsigned i){lfs_lazy_log("Native event",0x0100152000022000,i,0,"/asset");lfs_lazy_log_flush();}
void native_log_route(uint64_t generation,uint64_t low,uint64_t high){lfs_lazy_log_route(generation,low,high);lfs_lazy_log_flush();}
void native_log_fault(int locked,int stage,int read_failure){locked_slot=locked;fail_stage=stage;report_read_failure=read_failure;}
void native_log_retry(void){lfs_lazy_log_flush();}
unsigned native_log_next_slot(void){return snapshot_state->next_slot;}
uint32_t native_log_result(void){return snapshot_state->last_result;}
uint64_t native_log_sequence(void){return snapshot_state->sequence;}
void native_log_report_failure(void){lazy.log_failed=1;lfs_lazy_log_flush();}
void native_log_busy(int busy){lazy.log_lock=(uint32_t)busy;}
void native_log_stop(void){
 assert(snapshot_open_count==snapshot_close_count);assert(snapshot_bytes<snapshot_open_count*(MOADER_LOG_CAPACITY+80));
 assert(((struct object *)context.log_file)->refs==1&&source->refs==1);
 release(context.log_file);context.log_file=0;free(report_memory);free(event_buffer);free(snapshot_state);lazy.log_snapshot=0;cleanup();
}
