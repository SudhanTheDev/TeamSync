#include "ConsoleLanSync.h"
#include "WorkspaceRecovery.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
namespace fs=std::filesystem;
using teamsync::ConsoleLanSync;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<text;require(static_cast<bool>(out),"Cannot write test fixture");}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});}
void largeFile(const fs::path& path,std::size_t bytes){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);std::string block(65536,'x');for(std::size_t offset=0;offset<bytes;offset+=block.size())out.write(block.data(),static_cast<std::streamsize>(std::min(block.size(),bytes-offset)));require(static_cast<bool>(out),"Cannot write large fixture");}
std::uint64_t partialBytes(const fs::path& root){std::uint64_t n=0;const auto folder=root/".teamsync"/"transfer-cache";if(fs::exists(folder))for(const auto& item:fs::directory_iterator(folder))if(item.is_regular_file())n+=item.file_size();return n;}

#ifdef _WIN32
using Socket=SOCKET;
void closeSocket(Socket s){closesocket(s);}
#else
using Socket=int;
void closeSocket(Socket s){close(s);}
#endif
void u32(std::vector<char>& b,std::uint32_t v){for(int n=24;n>=0;n-=8)b.push_back(static_cast<char>(v>>n));}
void u64(std::vector<char>& b,std::uint64_t v){for(int n=56;n>=0;n-=8)b.push_back(static_cast<char>(v>>n));}
void string(std::vector<char>& b,const std::string& v){u32(b,static_cast<std::uint32_t>(v.size()));b.insert(b.end(),v.begin(),v.end());}
bool receive(Socket s,char* p,std::size_t n){while(n){const auto got=::recv(s,p,static_cast<int>(n),0);if(got<=0)return false;p+=got;n-=static_cast<std::size_t>(got);}return true;}
bool send(Socket s,const char* p,std::size_t n){while(n){const auto sent=::send(s,p,static_cast<int>(n),0);if(sent<=0)return false;p+=sent;n-=static_cast<std::size_t>(sent);}return true;}
std::map<std::string,std::string> wireHashes(std::uint16_t port,const std::string& code){
    const auto socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    require(connect(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"Connect checksum fixture");
    std::vector<char> request;string(request,"TEAMSYNC/3");string(request,"status");string(request,code);string(request,"checksum-test-client");u64(request,0);u32(request,0);
    std::vector<char> header;u32(header,static_cast<std::uint32_t>(request.size()));require(send(socket,header.data(),header.size())&&send(socket,request.data(),request.size()),"Send checksum query");
    char raw[4]{};require(receive(socket,raw,4),"Checksum response header");std::uint32_t size=0;for(char c:raw)size=(size<<8)|static_cast<unsigned char>(c);require(size<1024*1024,"Checksum response size");std::vector<char>b(size);require(receive(socket,b.data(),size),"Checksum response body");closeSocket(socket);
    std::size_t p=0;const auto byte=[&](){require(p<b.size(),"Checksum response parser");return static_cast<unsigned char>(b[p++]);};
    const auto number=[&](){std::uint32_t v=0;for(int n=0;n<4;++n)v=(v<<8)|byte();return v;};
    const auto text=[&](){const auto n=number();require(n<=b.size()-p,"Checksum text length");std::string value(b.data()+p,n);p+=n;return value;};
    require(byte()==1,"Checksum query rejected");text();for(int n=0;n<8;++n)byte();const auto count=number();std::map<std::string,std::string> result;
    for(std::uint32_t n=0;n<count;++n){const auto path=text();for(int i=0;i<8;++i)byte();result[path]=text();}return result;
}

// An untrusted host must not be able to install a file outside a managed
// directory. This exercises the real protocol parser, not a copied validator.
void unsafeHostTest(const fs::path& root,const std::string& badPath){
    ConsoleLanSync client(root);const Socket listener=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    require(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"Bind malicious-host fixture");
    require(listen(listener,1)==0,"Listen malicious-host fixture");socklen_t length=sizeof(address);getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length);
    std::thread server([=]{Socket peer=accept(listener,nullptr,nullptr);char header[4]{};if(receive(peer,header,4)){std::uint32_t n=0;for(char c:header)n=(n<<8)|static_cast<unsigned char>(c);if(n<1024*1024){std::vector<char> request(n);if(receive(peer,request.data(),n)){std::vector<char> b;b.push_back(1);string(b,"");u64(b,1);u32(b,1);string(b,badPath);u64(b,0);string(b,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");std::vector<char> frame;u32(frame,static_cast<std::uint32_t>(b.size()));send(peer,frame.data(),frame.size());send(peer,b.data(),b.size());}}}closeSocket(peer);closeSocket(listener);});
    std::string error;const bool joined=client.join("127.0.0.1",ntohs(address.sin_port),"123456",error);server.join();require(!joined,"Unsafe host manifest was accepted: "+badPath);require(error.find("Unsafe")!=std::string::npos,"Unsafe path should have a clear diagnostic: "+error);
}
}

int main(){
    const auto base=fs::temp_directory_path()/("teamsync-native-sync-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        const auto hostRoot=base/"host",firstRoot=base/"first",secondRoot=base/"second";
        write(hostRoot/"data"/"users.dat","initial users\n");write(hostRoot/"data"/"tasks.dat","initial tasks\n");
        write(hostRoot/"data"/"sha.dat","abc");write(hostRoot/"data"/"empty.dat","");
        write(hostRoot/"shared_files_storage"/"unchanged.txt","same content\n");
        ConsoleLanSync host(hostRoot),first(firstRoot),second(secondRoot);std::string code,error;require(host.host(0,code,error),error);
        const auto port=host.port();require(port!=0,"Port zero must resolve to an assigned port");
        const auto hashes=wireHashes(port,code);
        require(hashes.at("data/sha.dat")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","Streaming SHA-256 does not match the FIPS abc vector");
        require(hashes.at("data/empty.dat")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","Streaming SHA-256 does not match the empty vector");
        // Exercise request-local initialization and teardown repeatedly. This
        // catches Windows worker-exit failures before any large transfer.
        for (int request = 0; request < 32; ++request)
            require(wireHashes(port,code)==hashes,"Short-lived worker changed the host manifest");
        const auto peers=host.peerAddresses();
        require(std::find(peers.begin(),peers.end(),"127.0.0.1")!=peers.end(),"Authenticated active peer was not reported");
        require(!first.join("127.0.0.1",port,"incorrect",error),"Wrong pairing code was accepted");
        require(first.join("127.0.0.1",port,code,error),error);require(second.join("127.0.0.1",port,code,error),error);
        require(read(firstRoot/"data"/"users.dat")=="initial users\n","Initial download did not install data");
        require(host.fingerprint()==first.fingerprint(),"Initial manifests differ");
        const auto unchangedTime=fs::last_write_time(firstRoot/"shared_files_storage"/"unchanged.txt");
        write(hostRoot/"data"/"tasks.dat","host updated task status\n");host.noteLocalChange();bool changed=false;
        require(first.pull(changed,error)&&changed,error);require(first.progress().transferred==25,"Pull should send only the one changed file");
        require(fs::last_write_time(firstRoot/"shared_files_storage"/"unchanged.txt")==unchangedTime,"Unchanged file was unnecessarily rewritten");
        require(first.pull(changed,error)&&!changed,error);require(first.progress().transferred==0,"Unchanged pull should transfer no file bytes");

        write(secondRoot/"data"/"tasks.dat","unsynced local work\n");second.noteLocalChange();
        require(!second.pull(changed,error),"Pull overwrote pending local changes");
        require(read(secondRoot/"data"/"tasks.dat")=="unsynced local work\n","Pending work was changed");
        require(!second.push(error),"Stale client push should conflict");
        require(error.find("Conflict")!=std::string::npos,"Conflict diagnostic missing");
        const auto conflicts=teamsync::WorkspaceRecovery(secondRoot).listConflicts();require(!conflicts.empty(),"Conflict did not create a local recovery copy");
        require(!second.push(error),"Repeated stale push should still conflict");
        require(teamsync::WorkspaceRecovery(secondRoot).listConflicts().size()==conflicts.size(),"Identical conflict retries should not duplicate large snapshots");
        require(second.adoptHostAfterPreservingLocal(changed,error)&&changed,error);
        require(read(secondRoot/"data"/"tasks.dat")=="host updated task status\n","Explicit host resolution failed");

        write(firstRoot/"data"/"tasks.dat","client completed task\n");first.noteLocalChange();require(first.push(error),error);
        require(read(hostRoot/"data"/"tasks.dat")=="client completed task\n","Client upload did not reach host");
        require(second.pull(changed,error)&&changed,error);
        fs::remove(firstRoot/"shared_files_storage"/"unchanged.txt");first.noteLocalChange();require(first.push(error),error);
        require(!fs::exists(hostRoot/"shared_files_storage"/"unchanged.txt"),"Managed-file deletion did not synchronize");

        // Restart with pending work must authenticate without replacing it.
        write(secondRoot/"data"/"tasks.dat","offline queue after restart\n");second.noteLocalChange();second.disconnect();
        {
            ConsoleLanSync restarted(secondRoot);require(restarted.hasPendingChanges(),"Pending state did not survive restart");
            require(restarted.join("127.0.0.1",port,code,error),error);
            require(read(secondRoot/"data"/"tasks.dat")=="offline queue after restart\n","Reconnect lost offline queue");
            require(restarted.hasPendingChanges(),"Reconnect cleared offline pending state");
            require(!restarted.pull(changed,error),"Reconnect pull must protect offline queue");
            require(restarted.adoptHostAfterPreservingLocal(changed,error),error);
        }

        constexpr std::size_t fileSize=16*1024*1024;
        largeFile(hostRoot/"shared_files_storage"/"large.bin",fileSize);host.noteLocalChange();
        std::atomic_bool cancelled{false};first.setProgressHandler([&](const ConsoleLanSync::TransferProgress& p){if(p.active&&p.phase=="Receiving"&&p.transferred>=512*1024&&!cancelled.exchange(true))first.cancelTransfer();});
        require(!first.pull(changed,error),"Cancelled receive should return retryable failure");
        require(cancelled,"Cancellation hook did not run");require(!fs::exists(firstRoot/"shared_files_storage"/"large.bin"),"Partial transfer reached live workspace");
        const auto partial=partialBytes(firstRoot);require(partial>=512*1024,"Cancelled receive did not keep resume data");
        first.setProgressHandler({});require(first.pull(changed,error)&&changed,error);
        require(fs::file_size(firstRoot/"shared_files_storage"/"large.bin")==fileSize,"Resumed file size differs");
        require(first.progress().transferred<fileSize,"Resume unnecessarily resent the entire file");
        require(host.fingerprint()==first.fingerprint(),"Resumed SHA-256 manifest differs");
        require(partialBytes(firstRoot)==0,"Completed download cache was not cleaned up");
        require(fs::is_empty(firstRoot/".teamsync"/"transactions"),"Committed transactions retain duplicate files");

        // Cancellation after the last chunk still must not install data. The
        // completed verified part can be reused after process/engine restart.
        write(hostRoot/"task_attachments"/"17"/"final-chunk.txt","cancel-at-the-end");host.noteLocalChange();
        std::atomic_bool finalCancelled{false};
        first.setProgressHandler([&](const ConsoleLanSync::TransferProgress& p){if(p.phase=="Receiving"&&p.active&&p.total&&p.transferred==p.total&&!finalCancelled.exchange(true))first.cancelTransfer();});
        require(!first.pull(changed,error),"Final-chunk cancellation installed a file");
        require(!fs::exists(firstRoot/"task_attachments"/"17"/"final-chunk.txt"),"Cancelled final chunk leaked into live files");
        first.setProgressHandler({});first.disconnect();
        {
            ConsoleLanSync resumed(firstRoot);require(resumed.join("127.0.0.1",port,code,error),error);
            require(resumed.progress().transferred==0,"Restart should reuse the fully received final chunk");
            require(read(firstRoot/"task_attachments"/"17"/"final-chunk.txt")=="cancel-at-the-end","Restart resume did not install verified data");
        }
        require(first.join("127.0.0.1",port,code,error),error);

        write(firstRoot/"task_attachments"/"18"/"upload.bin",std::string(2*1024*1024,'y'));first.noteLocalChange();
        std::atomic_bool uploadCancelled{false};
        first.setProgressHandler([&](const ConsoleLanSync::TransferProgress& p){if(p.phase=="Sending"&&p.active&&p.transferred>=512*1024&&!uploadCancelled.exchange(true))first.cancelTransfer();});
        require(!first.push(error),"Cancelled upload should stay pending");
        require(first.hasPendingChanges(),"Cancelled upload discarded pending state");first.setProgressHandler({});
        require(first.push(error),error);require(first.progress().transferred<2*1024*1024,"Upload retry did not resume host-side cache");
        require(read(hostRoot/"task_attachments"/"18"/"upload.bin")==std::string(2*1024*1024,'y'),"Resumed upload content differs");
        require(partialBytes(hostRoot)==0,"Completed upload cache was not cleaned up");

        // A deliberately slow download must not starve another workstation or
        // UDP discovery. Each request has its own counters and partial cache.
        largeFile(hostRoot/"shared_files_storage"/"concurrent.bin",fileSize);host.noteLocalChange();
        ConsoleLanSync slow(base/"slow");std::atomic_bool slowReceiving{false},slowFinished{false};
        slow.setProgressHandler([&](const ConsoleLanSync::TransferProgress& p){if(p.phase=="Receiving"&&p.active&&p.transferred){slowReceiving=true;std::this_thread::sleep_for(std::chrono::milliseconds(40));}});
        std::string slowError;auto slowJoin=std::async(std::launch::async,[&]{const auto ok=slow.join("127.0.0.1",port,code,slowError);slowFinished=true;return ok;});
        const auto waitUntil=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(!slowReceiving&&std::chrono::steady_clock::now()<waitUntil)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(slowReceiving,"Slow client did not begin receiving");
        const auto concurrentStart=std::chrono::steady_clock::now();
        require(first.pull(changed,error)&&changed,error);
        require(!slowFinished,"A slow file transfer blocked another client's sync");
        require(std::chrono::steady_clock::now()-concurrentStart<std::chrono::seconds(4),"Concurrent client waited too long");
        const auto duringTransfer=ConsoleLanSync::discoverNearby(250);bool discoverable=false;for(const auto& item:duringTransfer)if(item.port==port)discoverable=true;
        require(discoverable,"Discovery was blocked by a large active transfer");
        require(slowJoin.get(),slowError);slow.disconnect();

        unsafeHostTest(base/"unsafe-traversal","../outside.dat");
        unsafeHostTest(base/"unsafe-absolute","C:/outside.dat");
        unsafeHostTest(base/"unsafe-settings","settings/secret.dat");
        unsafeHostTest(base/"unsafe-device","shared_files_storage/CON.txt");

        const auto crashRoot=base/"crash-recovery";const auto tx=crashRoot/".teamsync"/"transactions"/"123456789000-1";
        write(crashRoot/"data"/"tasks.dat","half-installed state");write(crashRoot/"data"/"new.dat","abc");
        write(tx/"original"/"data"/"tasks.dat","before transfer");write(tx/"APPLYING.txt","prepared");
        write(tx/"journal.txt","data/tasks.dat\t-\ndata/new.dat\tba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n");
        {ConsoleLanSync recovered(crashRoot);require(read(crashRoot/"data"/"tasks.dat")=="before transfer","Interrupted transaction did not restore original");require(!fs::exists(crashRoot/"data"/"new.dat"),"Interrupted new file did not roll back");require(!teamsync::WorkspaceRecovery(crashRoot).listConflicts().empty(),"Crash recovery did not preserve intermediate state");}

        const auto discovery=ConsoleLanSync::discoverNearby(350);bool found=false;for(const auto& item:discovery)if(item.port==port)found=true;
        require(found,"Native shared-protocol host was not discoverable on loopback");
        host.disconnect();first.disconnect();second.disconnect();
        std::cout<<"Native sync tests passed: interop, changed-only transfer, zero-byte no-op, CAS conflicts, recovery, offline restart, deletion, cancellation/resume, SHA-256, unsafe paths, concurrent clients, discovery during transfer.\n";
        std::cout<<"Isolated fixtures retained at "<<base.string()<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"Native sync test failed: "<<e.what()<<"\nFixtures: "<<base.string()<<'\n';return 1;}
}
