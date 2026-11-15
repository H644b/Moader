/* SPDX-License-Identifier: GPL-2.0-only
 * Moader: dynamic catalogues over an owned SD filesystem. All swaps happen
 * under plan_lock; successful plans retain the catalogue that built them.
 */
#include "moader.h"
#include "moader/runtime_wire.h"
#define BAD_CATALOGUE 0xd401u
struct lfs_moader_catalogue {
 struct lfs_arena *arena;
 uint64_t references,bytes,generation,count;
 uint32_t crc,receipt_pending;
 struct lfs_lazy_game *games;
};
struct lfs_moader_catalogue *lfs_moader_current(void){return lfs_lazy_config()?lfs_lazy_config()->moader_catalogue:0;}
void lfs_moader_retain(struct lfs_moader_catalogue *c){if(c)__atomic_fetch_add(&c->references,1,__ATOMIC_RELAXED);}
void lfs_moader_release(struct lfs_moader_catalogue *c){
 if(c&&__atomic_fetch_sub(&c->references,1,__ATOMIC_ACQ_REL)==1){
  __atomic_fetch_sub(&lfs_lazy_config()->moader_retained_bytes,c->bytes,__ATOMIC_RELAXED);lfs_arena_free(c->arena);
 }
}
const struct lfs_lazy_game *lfs_moader_game(struct lfs_moader_catalogue *c,uint64_t program){
 if(!c)return 0;
 for(uint64_t i=0;i<c->count;i++)if(c->games[i].program_id==program)return &c->games[i];
 uint64_t kind=program>>56;
 if(kind==1||kind==4)for(uint64_t i=0;i<c->count;i++)if(((c->games[i].program_id^program)&UINT64_C(0x00ffffffffffffff))==0)return &c->games[i];
 return 0;
}
static uint32_t read_exact(void *file,uint64_t offset,void *data,uint64_t bytes){
 struct ns_buffer buffer={data,bytes};uint64_t read=0;
 uint32_t result=lfs_context()->sdk.file_read(file,&read,(int64_t)offset,&buffer,(int64_t)bytes,0);
 return result?result:read==bytes?0:BAD_CATALOGUE;
}
static int prefix(const char *text,const char *start){for(uint64_t i=0;start[i];i++)if(!text[i]||text[i]!=start[i])return 0;return 1;}
static const char *text_at(const uint8_t *bytes,const struct MoaderCatalogueHeader *h,uint32_t at){
 if(at<h->strings_offset||at>=h->total_size)return 0;
 const char *text=(const char *)bytes+at;
 uint64_t length=0,limit=h->total_size-at;if(limit>0x301)limit=0x301;
 while(length<limit&&text[length])length++;
 if(!length||length==limit||length>0x300)return 0;
 return text;
}
static int safe_path(const char *path,int absolute){
 uint64_t length=lfs_text_length(path),start=absolute?1:0;
 if(!length||length>0x300||(absolute&&path[0]!='/')||(!absolute&&path[0]=='/'))return 0;
 for(uint64_t i=start;i<=length;i++){
  unsigned char c=(unsigned char)path[i];
  if(i<length&&(c<32||c==127||c=='\\'||c==':'))return 0;
  if(i==length||c=='/'){
   uint64_t n=i-start;
   if(!n||(n==1&&path[start]=='.')||(n==2&&path[start]=='.'&&path[start+1]=='.')||path[i-1]=='.'||path[i-1]==' ')return 0;
   start=i+1;
  }
 }
 return 1;
}
static int compare(const char *a,const char *b){
 for(uint64_t i=0;i<=0x300;i++){
  unsigned char x=a[i],y=b[i];if(x>='A'&&x<='Z')x+='a'-'A';if(y>='A'&&y<='Z')y+='a'-'A';
  if(x!=y)return x<y?-1:1;if(!x)return 0;
 }
 return 0;
}
static uint32_t parse(void *file,const struct MoaderCatalogueHeader *header,struct lfs_moader_catalogue **out){
 struct lfs_arena *arena=0;
 struct lfs_moader_catalogue *c=lfs_arena_allocate(&arena,sizeof(*c));
 uint8_t *bytes=c?lfs_arena_allocate(&arena,header->total_size):0;
 uint32_t result=bytes?read_exact(file,0,bytes,header->total_size):BAD_CATALOGUE;
 if(!result&&(!lfs_equal(bytes,header,sizeof(*header))||moader_crc32(bytes+sizeof(*header),header->total_size-sizeof(*header))!=header->payload_crc))result=BAD_CATALOGUE;
 struct lfs_lazy_file *files=0;struct lfs_lazy_game *games=0;
 if(!result&&header->rows){
  files=lfs_arena_allocate(&arena,header->rows*sizeof(*files));
  games=lfs_arena_allocate(&arena,512*sizeof(*games));
  if(!files||!games)result=BAD_CATALOGUE;
 }
 uint64_t previous_title=0;uint32_t previous_kind=0;const char *previous_name=0;
 const struct MoaderCatalogueRow *rows=(const struct MoaderCatalogueRow *)(bytes?bytes+sizeof(*header):0);
 for(uint32_t i=0;!result&&i<header->rows;i++){
  const struct MoaderCatalogueRow *r=&rows[i];
  const char *name=text_at(bytes,header,r->name_offset),*source=text_at(bytes,header,r->source_offset);
  uint64_t kind=r->title_id>>56;
  if(!name||!source||(kind!=1&&kind!=4)||(r->title_id&UINT64_C(0x00ffffffffffffff))<0x10000||r->reserved||r->size>64*1024*1024||
    !(r->kind==LAZY_ROMFS||r->kind==LAZY_EXEFS||r->kind==LAZY_ROMFS_IMAGE||r->kind==LAZY_EXEFS_IMAGE||r->kind==LAZY_PATCH)||
    !safe_path(name,r->kind==LAZY_ROMFS)||!safe_path(source,1)||!prefix(source,"/moader/library/")||
    (r->kind==LAZY_EXEFS&&prefix(name,"/"))||
    (r->kind==LAZY_ROMFS_IMAGE&&!lfs_text_equal(name,"romfs.bin"))||
    (r->kind==LAZY_EXEFS_IMAGE&&!lfs_text_equal(name,"exefs.nsp"))||
    r->title_id<previous_title||(r->title_id==previous_title&&(r->kind<previous_kind||(r->kind==previous_kind&&compare(previous_name,name)>=0)))){result=BAD_CATALOGUE;break;}
  if(r->title_id!=previous_title){
   if(c->count==512){result=BAD_CATALOGUE;break;}
   games[c->count].program_id=r->title_id;games[c->count].files=&files[i];c->count++;
  }
  files[i].name=name;files[i].source.path=source;files[i].size=r->size;files[i].kind=r->kind;
  games[c->count-1].count++;
  previous_title=r->title_id;previous_kind=r->kind;previous_name=name;
 }
 if(result){lfs_arena_free(arena);return result;}
 c->arena=arena;c->references=1;c->games=games;c->generation=header->generation;c->crc=header->payload_crc;
 c->bytes=header->total_size+sizeof(*c)+header->rows*sizeof(*files)+(header->rows?512*sizeof(*games):0);
 __atomic_fetch_add(&lfs_lazy_config()->moader_retained_bytes,c->bytes,__ATOMIC_RELAXED);
 *out=c;return 0;
}
static uint32_t receipt(const struct MoaderCatalogueHeader *h,uint32_t result){
 struct lfs_lazy_config *a=lfs_lazy_config();struct lfs_context *c=lfs_context();
 if(!c->write||!c->flush)return BAD_CATALOGUE;
 struct MoaderRuntimeAck ack={0};lfs_copy(ack.magic,MOADER_ACK_MAGIC,8);
 ack.version=MOADER_WIRE_VERSION;ack.result=result;ack.generation=a->moader_generation;
 ack.epoch_low=h->epoch_low;ack.epoch_high=h->epoch_high;ack.reloads=a->moader_reloads;
 ack.retained_bytes=__atomic_load_n(&a->moader_retained_bytes,__ATOMIC_RELAXED);ack.flags=1;
 ack.crc=moader_crc32(&ack,offsetof(struct MoaderRuntimeAck,crc));
 void *file=0;struct ns_out out={&file};
 uint32_t rc=c->open_file((uint8_t *)c->source_fs+8,&out,"/moader/runtime/ack.bin",3);
 if(!rc&&!file)rc=BAD_CATALOGUE;
 if(!rc){struct ns_buffer buffer={&ack,sizeof(ack)};rc=c->write(file,0,&buffer,sizeof(ack),0);if(!rc)rc=c->flush(file);}
 if(file)c->sdk.file_release(file);
 return rc;
}
void lfs_moader_reload_locked(void){
 struct lfs_lazy_config *a=lfs_lazy_config();struct lfs_context *c=lfs_context();
 if(!a||!a->moader_enabled)return;
 void *file=0;struct ns_out out={&file};struct MoaderCatalogueHeader h={0};uint64_t size=0;
 uint32_t result=c->open_file((uint8_t *)c->source_fs+8,&out,"/moader/runtime/catalogue.bin",1);
 if(result||!file)return;
 result=c->sdk.file_size(file,&size);
 if(!result&&(size<sizeof(h)||size>MOADER_MAX_CATALOGUE))result=BAD_CATALOGUE;
 if(!result)result=read_exact(file,0,&h,sizeof(h));
 int identified=!result&&lfs_equal(h.magic,MOADER_CATALOGUE_MAGIC,8)&&h.version==MOADER_WIRE_VERSION;
 if(!result&&(!identified||h.rows>MOADER_MAX_ROWS||h.total_size!=size||h.strings_offset!=sizeof(h)+h.rows*sizeof(struct MoaderCatalogueRow)||h.strings_offset>h.total_size||h.reserved||h.reserved2||!(h.epoch_low|h.epoch_high)))result=BAD_CATALOGUE;
 struct lfs_moader_catalogue *current=a->moader_catalogue,*next=0;
 int same_epoch=h.epoch_low==a->moader_epoch_low&&h.epoch_high==a->moader_epoch_high;
 if(!result&&current&&same_epoch&&h.generation==a->moader_generation&&h.payload_crc==current->crc){
  c->sdk.file_release(file);
  // Retrying a failed receipt must not reload routing, allocate another
  // catalogue or write SD on every successful wake. A rejected publication
  // may also have overwritten the receipt for this still-active catalogue.
  if(current->receipt_pending||a->moader_result){a->moader_result=0;current->receipt_pending=receipt(&h,0)!=0;}
  return;
 }
 if(!result&&current&&same_epoch&&h.generation<=a->moader_generation)result=BAD_CATALOGUE;
 if(!result)result=parse(file,&h,&next);
 c->sdk.file_release(file);
 if(!result){
  a->moader_catalogue=next;a->moader_generation=h.generation;a->moader_epoch_low=h.epoch_low;a->moader_epoch_high=h.epoch_high;
  lfs_lazy_log_route(h.generation,h.epoch_low,h.epoch_high);
  a->count=next->count;a->games=next->games;a->patch_count=0;a->patches=0;a->moader_reloads++;
  lfs_moader_release(current);
 }
 a->moader_result=result;
 if(identified){
  uint32_t rc=receipt(&h,result);
  if(a->moader_catalogue)((struct lfs_moader_catalogue *)a->moader_catalogue)->receipt_pending=result||rc;
 }
 lfs_lazy_log(result?"Moader catalogue rejected":"Moader routing published",0,h.generation,result,0);lfs_lazy_log_flush();
}
void lfs_moader_poll(void){
 struct lfs_lazy_config *a=lfs_lazy_config();
 if(a&&a->moader_enabled){lfs_lazy_plan_lock();lfs_moader_reload_locked();lfs_lazy_plan_unlock();lfs_lazy_log_flush();}
}
