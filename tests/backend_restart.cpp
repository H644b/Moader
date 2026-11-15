// Emit catalogues from two actual service-core lifetimes. The separate native
// executable consumes both in one FS runtime; this is an offline host seam.
#include "moader/runtime_backend.hpp"
#include "moader/runtime_wire.h"
#include "moader/library.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static std::string read_file(const std::string& path) {
    std::ifstream file(path,std::ios::binary);assert(file);return {(std::istreambuf_iterator<char>(file)),{}};
}
static MoaderCatalogueHeader header(const std::string& bytes) {
    assert(bytes.size()>=sizeof(MoaderCatalogueHeader));MoaderCatalogueHeader value{};
    std::memcpy(&value,bytes.data(),sizeof(value));return value;
}
static void receipt_status(const std::string& root,const std::string& file,bool accepted,bool wrong_epoch=false) {
    // RuntimeBackend reads the production C writer's exact receipt. Copy into
    // the existing inode; do not manufacture or reinterpret success here.
    const auto bytes=read_file(root+'/'+file);assert(bytes.size()==sizeof(MoaderRuntimeAck));
    const int fd=::open((root+"/runtime/ack.bin").c_str(),O_WRONLY);assert(fd>=0);
    assert(::pwrite(fd,bytes.data(),bytes.size(),0)==static_cast<ssize_t>(bytes.size()));assert(!::close(fd));
    moader::RuntimeBackend backend(root,{33,44});const auto status=backend.status();
    if(wrong_epoch)assert(!status.connected);
    else {assert(status.connected);assert(bool(status.result)==!accepted);}
}
int main(int argc,char** argv) {
    assert(argc>=3);const std::string mode=argv[1],root=argv[2];
    if(mode=="verify") {
        receipt_status(root,"first-ack.bin",false,true);
        receipt_status(root,"rejected-ack.bin",false);
        receipt_status(root,"second-ack.bin",true);
        std::cout<<"Actual RuntimeBackend rejects the previous epoch, reports the native rejection and accepts the fresh native receipt.\n";
        return 0;
    }
    assert(mode=="emit"&&argc==4);const std::string title="04000000000d0001";
    std::filesystem::create_directories(root);
    moader::atomic_write(root+"/pairing-code","001234");
    std::string saved_preferences,saved_enabled;uint64_t saved_generation=0;
    int retained_receipt=-1;struct stat original{};
    {
        moader::RuntimeBackend backend(root,{11,22});
        moader::Library library(root,backend);
        library.import_zip(title,"sample",argv[3],"Restart fixture");
        library.set_enabled(title,"sample",true);
        auto preferences=library.preferences();preferences.game_names=false;
        preferences.reduced_motion=true;preferences.web_on_start=true;preferences.web_port=17812;preferences.follow_log=false;
        library.set_preferences(preferences);
        saved_preferences=read_file(root+"/preferences");
        saved_enabled=read_file(root+"/library/"+title+"/sample/.enabled");
        saved_generation=library.generation();assert(!backend.status().connected);
        std::filesystem::copy_file(root+"/runtime/catalogue.bin",root+"/first.bin");
        retained_receipt=::open((root+"/runtime/ack.bin").c_str(),O_RDWR);assert(retained_receipt>=0);
        assert(!::fstat(retained_receipt,&original));
    } // Both Library and RuntimeBackend really leave scope before reconstruction.
    {
        moader::RuntimeBackend backend(root,{33,44});
        struct stat current{},held{};assert(!::stat((root+"/runtime/ack.bin").c_str(),&current));
        assert(!::fstat(retained_receipt,&held));
        assert(current.st_ino==original.st_ino&&current.st_dev==original.st_dev&&held.st_ino==current.st_ino);
        moader::Library library(root,backend);
        auto view=library.snapshot();assert(view.inventory_ready&&view.inventory_error.empty());
        assert(view.mods.size()==1&&view.mods[0].requested&&view.mods[0].id=="sample");
        assert(library.generation()==saved_generation&&!backend.status().connected);
        const auto preferences=library.preferences();assert(!preferences.game_names&&preferences.reduced_motion&&preferences.web_on_start&&preferences.web_port==17812&&!preferences.follow_log);
        assert(read_file(root+"/preferences")==saved_preferences);
        assert(read_file(root+"/pairing-code")=="001234");
        assert(read_file(root+"/library/"+title+"/sample/.enabled")==saved_enabled);
        library.synchronize();
        std::filesystem::copy_file(root+"/runtime/catalogue.bin",root+"/second.bin");
        const auto first=read_file(root+"/first.bin"),second=read_file(root+"/second.bin");
        const auto a=header(first),b=header(second);
        assert(a.rows==1&&b.rows==1&&a.generation==b.generation&&a.generation==saved_generation);
        assert(a.epoch_low==11&&a.epoch_high==22&&b.epoch_low==33&&b.epoch_high==44);
        assert(first.substr(sizeof(a))==second.substr(sizeof(b))); // Epoch alone must cause native acceptance.
        assert(!backend.status().connected);
        assert(!::stat((root+"/runtime/ack.bin").c_str(),&current)&&current.st_ino==original.st_ino);
    }
    assert(!::close(retained_receipt));
    std::cout<<"Real Library/RuntimeBackend restart preserved requests, preferences, pairing, generation and receipt inode; emitted distinct epochs.\n";
}
