// Use the existing SDK object mocks, but exercise the full production C
// catalogue reader and namespace/lifetime implementations in one live runtime.
#define main unused_native_regression_main
#include "../runtime/modloader/native/verify-lazy.c"
#undef main
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int restart_ack_fd=-1;
static uint8_t *read_bytes(const char *path,uint64_t *size){
 FILE *file=fopen(path,"rb");assert(file);assert(!fseek(file,0,SEEK_END));long end=ftell(file);
 assert(end>0&&(uint64_t)end<=MOADER_MAX_CATALOGUE);rewind(file);*size=(uint64_t)end;
 uint8_t *bytes=malloc((size_t)end);assert(bytes&&fread(bytes,1,(size_t)end,file)==(size_t)end);assert(!fclose(file));return bytes;
}
static void snapshot_ack(const char *root,const char *name,const uint8_t *bytes){
 char path[4096];assert(snprintf(path,sizeof(path),"%s/%s",root,name)>0);
 FILE *file=fopen(path,"wb");assert(file&&fwrite(bytes,1,64,file)==64);assert(!fclose(file));
}
static uint32_t restart_flush(void *file){
 const struct object *o=file;assert(o->size==sizeof(struct MoaderRuntimeAck));
 uint32_t result=receipt_flush(file);if(result)return result;
 assert(pwrite(restart_ack_fd,o->data,(size_t)o->size,0)==(ssize_t)o->size);assert(!fsync(restart_ack_fd));return 0;
}
static struct MoaderRuntimeAck checked_ack(const uint8_t *bytes,uint64_t low,uint64_t high,int error){
 struct MoaderRuntimeAck ack;memcpy(&ack,bytes,sizeof(ack));
 assert(!memcmp(ack.magic,MOADER_ACK_MAGIC,8)&&ack.version==MOADER_WIRE_VERSION&&ack.flags==1);
 assert(ack.epoch_low==low&&ack.epoch_high==high&&!!ack.result==error);
 assert(ack.crc==moader_crc32(&ack,offsetof(struct MoaderRuntimeAck,crc)));return ack;
}
static void read_replacement(void *file,const uint8_t *expected){
 uint8_t bytes[4]={0};uint64_t count=0;struct ns_buffer buffer={bytes,sizeof(bytes)};
 assert(!lfs_file_read(file,&count,0,&buffer,sizeof(bytes),0)&&count==sizeof(bytes)&&!memcmp(bytes,expected,sizeof(bytes)));
}
int main(int argc,char **argv){
 assert(argc==2);const char *root=argv[1];assert(strlen(root)<2000);
 char path[4096];uint64_t first_size,second_size,payload_size;
 snprintf(path,sizeof(path),"%s/first.bin",root);uint8_t *first=read_bytes(path,&first_size);
 snprintf(path,sizeof(path),"%s/second.bin",root);uint8_t *second=read_bytes(path,&second_size);
 snprintf(path,sizeof(path),"%s/library/04000000000d0001/sample/exefs/main",root);uint8_t *payload=read_bytes(path,&payload_size);assert(payload_size==4);
 const struct MoaderCatalogueHeader *a=(const void *)first,*b=(const void *)second;
 assert(a->rows==1&&b->rows==1&&a->generation==b->generation&&a->epoch_low==11&&a->epoch_high==22&&b->epoch_low==33&&b->epoch_high==44);
 snprintf(path,sizeof(path),"%s/runtime/ack.bin",root);restart_ack_fd=open(path,O_RDWR);assert(restart_ack_fd>=0);
 struct stat original_inode,current_inode;assert(!fstat(restart_ack_fd,&original_inode));
 uint8_t ack_bytes[64]={0},native_bytes[4]={1,2,3,4};
 setup();inputs[0]=(struct input){"/moader/runtime/catalogue.bin",first,first_size};
 inputs[1]=(struct input){"/moader/runtime/ack.bin",ack_bytes,sizeof(ack_bytes)};
 inputs[2]=(struct input){"/moader/library/04000000000d0001/sample/exefs/main",payload,payload_size};input_count=3;
 codefiles[0]=(struct input){"/main",native_bytes,sizeof(native_bytes)};codecount=1;
 context.write=receipt_write;context.flush=restart_flush;lazy.moader_enabled=1;receipt_writes=receipt_flushes=0;
 lfs_moader_poll();assert(!lazy.moader_result&&lazy.moader_generation==a->generation&&lazy.count==1&&lazy.games[0].count==1);
 struct MoaderRuntimeAck ack=checked_ack(ack_bytes,11,22,0);assert(ack.reloads==1&&ack.generation==a->generation);
 snapshot_ack(root,"first-ack.bin",ack_bytes);
 lfs_namespace_register((uint8_t *)gamefs+8,UINT64_C(0x04000000000d0001));
 struct lfs_life *filesystem=lfs_lookup(gamefs,LFS_CODEFS);assert(filesystem&&filesystem->plan_owner);
 void *held_file=0;struct ns_out out={&held_file};assert(!lfs_namespace_open((uint8_t *)gamefs+8,&out,"/main",1,&ns)&&held_file);
 read_replacement(held_file,payload);
 void *first_catalogue=lazy.moader_catalogue;const uint64_t first_retained=lazy.moader_retained_bytes;
 // A malformed publication from the next service epoch cannot withdraw the
 // currently accepted routing or invalidate its outstanding file owner.
 uint8_t *malformed=malloc((size_t)second_size);assert(malformed);memcpy(malformed,second,(size_t)second_size);malformed[second_size-2]^=1;
 inputs[0]=(struct input){"/moader/runtime/catalogue.bin",malformed,second_size};lfs_moader_poll();
 assert(lazy.moader_result&&lazy.moader_catalogue==first_catalogue&&lazy.moader_epoch_low==11&&lazy.moader_epoch_high==22);
 assert(lazy.moader_generation==a->generation&&lazy.moader_reloads==1&&lazy.moader_retained_bytes==first_retained);
 ack=checked_ack(ack_bytes,33,44,1);assert(ack.generation==a->generation&&ack.reloads==1);
 snapshot_ack(root,"rejected-ack.bin",ack_bytes);read_replacement(held_file,payload);
 // Accept the valid second epoch at the SAME saved generation and payload.
 inputs[0]=(struct input){"/moader/runtime/catalogue.bin",second,second_size};lfs_moader_poll();
 assert(!lazy.moader_result&&lazy.moader_catalogue!=first_catalogue&&lazy.moader_epoch_low==33&&lazy.moader_epoch_high==44);
 assert(lazy.moader_generation==b->generation&&lazy.moader_reloads==2&&lazy.moader_retained_bytes>first_retained);
 ack=checked_ack(ack_bytes,33,44,0);assert(ack.generation==b->generation&&ack.reloads==2);
 snapshot_ack(root,"second-ack.bin",ack_bytes);
 const struct lfs_lazy_game *pinned=lfs_lazy_owner_game(filesystem->plan_owner,UINT64_C(0x04000000000d0001));
 assert(pinned&&pinned->count==1&&!strcmp(pinned->files[0].source.path,inputs[2].path));
 lfs_filesystem_release(gamefs);gamefs=object(3,0,0);read_replacement(held_file,payload);
 lfs_namespace_register((uint8_t *)gamefs+8,UINT64_C(0x04000000000d0001));
 void *new_file=0;out.value=&new_file;assert(!lfs_namespace_open((uint8_t *)gamefs+8,&out,"/main",1,&ns));read_replacement(new_file,payload);lfs_file_release(new_file);
 // Repeated wakes allocate nothing and do not rewrite an acknowledged receipt.
 const uint64_t steady_bytes=block_bytes,steady_retained=lazy.moader_retained_bytes,steady_writes=receipt_writes;
 for(unsigned i=0;i<1000;i++){lfs_moader_poll();assert(block_bytes==steady_bytes&&lazy.moader_retained_bytes==steady_retained&&lazy.moader_reloads==2&&receipt_writes==steady_writes);}
 lfs_file_release(held_file);assert(lazy.moader_retained_bytes<steady_retained);
 lfs_filesystem_release(gamefs);gamefs=object(3,0,0);
 lfs_moader_release(lazy.moader_catalogue);lazy.moader_catalogue=0;assert(!lazy.moader_retained_bytes);cleanup();
 assert(!fstat(restart_ack_fd,&current_inode)&&current_inode.st_ino==original_inode.st_ino&&current_inode.st_dev==original_inode.st_dev);
 assert(!close(restart_ack_fd));free(first);free(second);free(malformed);free(payload);
 fprintf(stderr,"Production native C reader: live epoch switch, malformed-catalogue retention/error receipt, fresh receipt, retained CodeFS file, 1000 bounded wakes and complete owner release passed.\n");return 0;
}
