#include "ConsoleLanSync.h"
#include "WorkspaceRecovery.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace teamsync {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr std::uint32_t chunkSize = 256 * 1024;
constexpr std::uint32_t maxPacket = 16 * 1024 * 1024;
constexpr std::uint32_t maxFiles = 50000;
constexpr std::uint16_t discoveryPort = 45455;
constexpr char protocol[] = "TEAMSYNC/3";
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalidSocket = INVALID_SOCKET;
void closeSocket(Socket s) { if (s != invalidSocket) closesocket(s); }
void stopSocket(Socket s) { if (s != invalidSocket) ::shutdown(s, SD_BOTH); }
struct SocketRuntime { SocketRuntime() { WSADATA d{}; WSAStartup(MAKEWORD(2, 2), &d); } ~SocketRuntime() { WSACleanup(); } };
#else
using Socket = int;
constexpr Socket invalidSocket = -1;
void closeSocket(Socket s) { if (s != invalidSocket) close(s); }
void stopSocket(Socket s) { if (s != invalidSocket) ::shutdown(s, SHUT_RDWR); }
struct SocketRuntime {};
#endif
SocketRuntime& socketRuntime() { static SocketRuntime runtime; return runtime; }
struct SocketGuard { Socket value = invalidSocket; ~SocketGuard() { closeSocket(value); } };

// Portable, streaming SHA-256 (FIPS 180-4). No dependency on Qt or whole-file
// buffers, including when validating a resumed transfer.
class Sha256 {
    std::array<std::uint32_t, 8> state_{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    std::array<unsigned char, 64> buffer_{};
    std::uint64_t bytes_ = 0;
    std::size_t used_ = 0;
    static std::uint32_t r(std::uint32_t v, unsigned n) { return (v >> n) | (v << (32 - n)); }
    void block(const unsigned char* p) {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64]{};
        for (unsigned i = 0; i < 16; ++i) for (unsigned j = 0; j < 4; ++j) w[i] = (w[i] << 8) | p[i * 4 + j];
        for (unsigned i = 16; i < 64; ++i) w[i] = w[i-16] + (r(w[i-15],7)^r(w[i-15],18)^(w[i-15]>>3)) + w[i-7] + (r(w[i-2],17)^r(w[i-2],19)^(w[i-2]>>10));
        auto a=state_[0], b=state_[1], c=state_[2], d=state_[3], e=state_[4], f=state_[5], g=state_[6], h=state_[7];
        for (unsigned i=0;i<64;++i) { const auto t1=h+(r(e,6)^r(e,11)^r(e,25))+((e&f)^((~e)&g))+k[i]+w[i]; const auto t2=(r(a,2)^r(a,13)^r(a,22))+((a&b)^(a&c)^(b&c)); h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
public:
    void add(const char* data, std::size_t n) { bytes_ += n; while (n) { const auto take=std::min(n,64-used_); std::memcpy(buffer_.data()+used_,data,take); used_+=take;data+=take;n-=take;if(used_==64){block(buffer_.data());used_=0;} } }
    std::string finish() { const auto bits=bytes_*8;buffer_[used_++]=0x80;if(used_>56){while(used_<64)buffer_[used_++]=0;block(buffer_.data());used_=0;}while(used_<56)buffer_[used_++]=0;for(int n=7;n>=0;--n)buffer_[used_++]=static_cast<unsigned char>(bits>>(n*8));block(buffer_.data());std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto v:state_)out<<std::setw(8)<<v;return out.str(); }
};
std::string hashFile(const fs::path& path,const std::function<bool()>& cancelled={}) { std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read workspace file: "+path.filename().string());Sha256 hash;std::array<char,chunkSize> b{};while(in){if(cancelled&&cancelled())throw std::runtime_error("Transfer cancelled during verification. Partial files are safe for Retry.");in.read(b.data(),b.size());hash.add(b.data(),static_cast<std::size_t>(in.gcount()));}if(!in.eof())throw std::runtime_error("Could not finish reading workspace file.");return hash.finish(); }
std::string manifestHash(const fs::path& path,std::uint64_t size) {
    struct CachedHash { std::uint64_t size;fs::file_time_type modified;std::string hash; };
    static std::mutex mutex;static std::map<fs::path,CachedHash> cache;
    const auto modified=fs::last_write_time(path);
    {
        std::lock_guard<std::mutex> lock(mutex);const auto found=cache.find(path);
        if(found!=cache.end()&&found->second.size==size&&found->second.modified==modified)return found->second.hash;
    }
    const auto hash=hashFile(path);
    if(fs::file_size(path)!=size||fs::last_write_time(path)!=modified)throw std::runtime_error("A workspace file changed while its manifest was being checked. Retry synchronization.");
    std::lock_guard<std::mutex> lock(mutex);if(cache.size()>100000)cache.clear();cache[path]={size,modified,hash};return hash;
}
std::string token() { return std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(std::random_device{}()); }
std::string endpointKey(const std::string& address,std::uint16_t port,const std::string& code) { Sha256 h;h.add(code.data(),code.size());return address+":"+std::to_string(port)+"#"+h.finish(); }
std::string timestamp() { const auto now=std::time(nullptr);std::tm t{};
#ifdef _WIN32
    localtime_s(&t,&now);
#else
    localtime_r(&now,&t);
#endif
    std::ostringstream out;out<<std::put_time(&t,"%Y-%m-%d %H:%M:%S");return out.str(); }

struct Writer {
    std::vector<char> bytes;
    void u8(std::uint8_t v){bytes.push_back(static_cast<char>(v));}
    void u32(std::uint32_t v){for(int n=24;n>=0;n-=8)bytes.push_back(static_cast<char>(v>>n));}
    void u64(std::uint64_t v){for(int n=56;n>=0;n-=8)bytes.push_back(static_cast<char>(v>>n));}
    void string(const std::string& v){if(v.size()>4096)throw std::runtime_error("Network text is too long.");u32(static_cast<std::uint32_t>(v.size()));bytes.insert(bytes.end(),v.begin(),v.end());}
};
struct Reader {
    const std::vector<char>& bytes;std::size_t p=0;
    std::uint8_t u8(){if(p>=bytes.size())throw std::runtime_error("Incomplete protocol message.");return static_cast<std::uint8_t>(bytes[p++]);}
    std::uint32_t u32(){std::uint32_t v=0;for(int i=0;i<4;++i)v=(v<<8)|u8();return v;}
    std::uint64_t u64(){std::uint64_t v=0;for(int i=0;i<8;++i)v=(v<<8)|u8();return v;}
    std::string string(){const auto n=u32();if(n>4096||n>bytes.size()-p)throw std::runtime_error("Invalid protocol text length.");std::string v(bytes.data()+p,n);p+=n;return v;}
    void end(){if(p!=bytes.size())throw std::runtime_error("Unexpected protocol data.");}
};
bool safeRelative(const std::string& name) {
    if(name.empty()||name.size()>2048||name.front()=='/'||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos)return false;
    std::istringstream in(name);std::string part;
    while(std::getline(in,part,'/')) { if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')return false;for(unsigned char c:part)if(c<32||c=='<'||c=='>'||c=='"'||c=='|'||c=='?'||c=='*')return false;auto base=part.substr(0,part.find('.'));std::transform(base.begin(),base.end(),base.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});if(base=="con"||base=="prn"||base=="aux"||base=="nul"||(base.size()==4&&(base.rfind("com",0)==0||base.rfind("lpt",0)==0)&&base[3]>='0'&&base[3]<='9'))return false; }
    if(name.back()=='/')return false;
    if(name.rfind("data/",0)==0)return name.find('/',5)==std::string::npos&&name.size()>9&&name.compare(name.size()-4,4,".dat")==0;
    return (name.rfind("task_attachments/",0)==0&&name.size()>17) || (name.rfind("shared_files_storage/",0)==0&&name.size()>21);
}
void noLinks(const fs::path& root,const fs::path& relative={}) {
    auto p=root;
    const auto check=[](const fs::path& v){std::error_code ec;const auto st=fs::symlink_status(v,ec);if(!ec&&fs::is_symlink(st))throw std::runtime_error("Linked paths are not allowed in synchronized workspaces.");
#ifdef _WIN32
        const auto a=GetFileAttributesW(v.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Junction/reparse paths are not allowed in synchronized workspaces.");
#endif
    };
    check(p);for(const auto& component:relative){p/=component;check(p);}
}
struct Entry { std::string path,hash;std::uint64_t size=0; };
using Manifest=std::map<std::string,Entry>;
Manifest scan(const fs::path& root) {
    Manifest result;noLinks(root);
    const auto add=[&](const fs::directory_entry& e){const auto rel=e.path().lexically_relative(root).generic_string();if(!safeRelative(rel))throw std::runtime_error("Unsafe managed workspace path: "+rel);noLinks(root,fs::path(rel));if(!e.is_regular_file())return;if(result.size()>=maxFiles)throw std::runtime_error("This workspace has too many managed files.");const auto size=e.file_size();if(size>(1ULL<<40))throw std::runtime_error("A managed file exceeds the 1 TiB safety limit.");result.emplace(rel,Entry{rel,manifestHash(e.path(),size),size});};
    const auto data=root/"data";noLinks(root,"data");if(fs::exists(data))for(const auto& e:fs::directory_iterator(data)){if(e.path().extension()==".dat")add(e);}
    for(const auto* folder:{"task_attachments","shared_files_storage"}){const auto base=root/folder;noLinks(root,folder);if(fs::exists(base))for(const auto& e:fs::recursive_directory_iterator(base)){noLinks(root,e.path().lexically_relative(root));if(e.is_symlink()){throw std::runtime_error("A linked attachment cannot be synchronized.");}if(e.is_regular_file())add(e);}}
    return result;
}
std::string digest(const Manifest& m){Sha256 h;for(const auto& item:m){h.add(item.first.data(),item.first.size());h.add("\0",1);h.add(item.second.hash.data(),item.second.hash.size());const auto size=std::to_string(item.second.size);h.add(size.data(),size.size());h.add("\n",1);}return h.finish();}
void writeManifest(Writer& w,const Manifest& m){w.u32(static_cast<std::uint32_t>(m.size()));for(const auto& i:m){w.string(i.first);w.u64(i.second.size);w.string(i.second.hash);}if(w.bytes.size()>maxPacket)throw std::runtime_error("Workspace manifest exceeds the 16 MiB protocol safety limit.");}
Manifest readManifest(Reader& r){Manifest m;std::set<std::string> aliases;const auto count=r.u32();if(count>maxFiles)throw std::runtime_error("Too many files in workspace manifest.");for(std::uint32_t i=0;i<count;++i){Entry e;e.path=r.string();e.size=r.u64();e.hash=r.string();if(!safeRelative(e.path)||e.size>(1ULL<<40)||e.hash.size()!=64||e.hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Unsafe workspace manifest entry.");auto alias=e.path;std::transform(alias.begin(),alias.end(),alias.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});if(!aliases.insert(alias).second||!m.emplace(e.path,e).second)throw std::runtime_error("Duplicate workspace path.");}return m;}
void timeout(Socket s) {
#ifdef _WIN32
    const DWORD ms=10000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&ms),sizeof(ms));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&ms),sizeof(ms));
#else
    const timeval t{10,0};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));
#endif
}
void sendAll(Socket s,const char* p,std::size_t n,const std::atomic_bool& cancelled){while(n){if(cancelled)throw std::runtime_error("Transfer cancelled. Retry resumes saved partial files.");const auto sent=::send(s,p,static_cast<int>(std::min<std::size_t>(n,chunkSize)),0);if(sent<=0)throw std::runtime_error("Network transfer interrupted. Retry resumes saved partial files.");p+=sent;n-=static_cast<std::size_t>(sent);}}
void receiveAll(Socket s,char* p,std::size_t n,const std::atomic_bool& cancelled){while(n){if(cancelled)throw std::runtime_error("Transfer cancelled. Retry resumes saved partial files.");const auto got=::recv(s,p,static_cast<int>(std::min<std::size_t>(n,chunkSize)),0);if(got<=0)throw std::runtime_error("Network transfer interrupted or timed out. Retry resumes saved partial files.");p+=got;n-=static_cast<std::size_t>(got);}}
void sendPacket(Socket s,const std::vector<char>& b,const std::atomic_bool& c){if(b.size()>maxPacket)throw std::runtime_error("Protocol frame exceeds safety limit.");Writer h;h.u32(static_cast<std::uint32_t>(b.size()));sendAll(s,h.bytes.data(),4,c);if(!b.empty())sendAll(s,b.data(),b.size(),c);}
std::vector<char> packet(Socket s,const std::atomic_bool& c,std::uint32_t limit=maxPacket){std::vector<char> h(4);receiveAll(s,h.data(),4,c);Reader r{h};const auto n=r.u32();if(n>limit)throw std::runtime_error("Incoming protocol frame exceeds safety limit.");std::vector<char>b(n);if(n)receiveAll(s,b.data(),n,c);return b;}
Socket connectTo(const std::string& host,std::uint16_t port){(void)socketRuntime();SocketGuard s{::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)};if(s.value==invalidSocket)throw std::runtime_error("Cannot create network socket.");sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);if(inet_pton(AF_INET,host.c_str(),&a.sin_addr)!=1){addrinfo hints{};hints.ai_family=AF_INET;addrinfo* result=nullptr;if(getaddrinfo(host.c_str(),nullptr,&hints,&result)!=0||!result)throw std::runtime_error("The host name/address could not be found.");a.sin_addr=reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;freeaddrinfo(result);}
#ifdef _WIN32
    u_long nonblocking=1;ioctlsocket(s.value,FIONBIO,&nonblocking);
#else
    const auto flags=fcntl(s.value,F_GETFL,0);fcntl(s.value,F_SETFL,flags|O_NONBLOCK);
#endif
    const auto result=::connect(s.value,reinterpret_cast<sockaddr*>(&a),sizeof(a));if(result!=0){fd_set writes;FD_ZERO(&writes);FD_SET(s.value,&writes);timeval wait{5,0};if(select(static_cast<int>(s.value+1),nullptr,&writes,nullptr,&wait)<=0)throw std::runtime_error("Could not reach host within 5 seconds. Check Wi-Fi/hotspot and Private-network firewall permission.");int err=0;socklen_t size=sizeof(err);getsockopt(s.value,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&err),&size);if(err)throw std::runtime_error("Host refused the connection. Check it is running and the connection code is current.");}
#ifdef _WIN32
    nonblocking=0;ioctlsocket(s.value,FIONBIO,&nonblocking);
#else
    fcntl(s.value,F_SETFL,flags);
#endif
    timeout(s.value);const auto value=s.value;s.value=invalidSocket;return value;
}
} // namespace

struct ConsoleLanSync::Impl {
    fs::path root;
    mutable std::recursive_mutex mutex;
    std::mutex operationMutex;
    mutable std::mutex callbackMutex;
    std::atomic_int mode{0}; // 0 local, 1 host, 2 client
    std::atomic_bool stopping{false},cancelled{false};
    std::atomic<std::uint64_t> cancellationGeneration{0};
    std::atomic<std::intptr_t> transferSocket{-1};
    Socket server=invalidSocket,discovery=invalidSocket;
    std::thread worker;
    struct PeerWorker { std::thread thread;std::shared_ptr<std::atomic_bool> done; };
    std::vector<PeerWorker> clientWorkers;
    std::mutex socketsMutex;
    std::set<Socket> activeSockets;
    std::uint16_t port=0;
    std::uint64_t revision=0;
    std::string address,code,baseline,hostDigest,endpoint,lastSync,lastConflictDigest,lastConflictId;
    std::string clientId=token();
    bool pending=false;
    std::vector<std::string> peers;
    std::map<std::string, Clock::time_point> peerSeen;
    std::map<std::string, int> peerActive;
    std::function<void()> remoteHandler;
    std::function<void(const TransferProgress&)> progressHandler;
    TransferProgress progress;
    static thread_local Clock::time_point started;
    static thread_local std::uint64_t sessionBytes,sessionGeneration;
    // Keep request-local TLS trivially destructible and allocation-free when
    // short-lived native socket workers exit. The bounded scope is always
    // 24 hex bytes plus '-' and its terminating zero.
    static thread_local std::array<char, 26> cacheScope;
    explicit Impl(fs::path p):root(fs::absolute(std::move(p)).lexically_normal()){(void)socketRuntime();recoverTransactions();loadState();}
    fs::path internal(const fs::path& p)const{noLinks(root,".teamsync");noLinks(root/".teamsync",p);return root/".teamsync"/p;}
    void removeCommittedTransaction(const fs::path& tx) const {
        const auto parent=internal("transactions");
        const auto id=tx.filename().string();
        if(tx.parent_path()!=parent||id.empty()||id.size()>96||id.find_first_not_of("0123456789-")!=std::string::npos)throw std::runtime_error("Unsafe synchronization cleanup target.");
        noLinks(parent,tx.filename());
        if(!fs::exists(tx/"COMMITTED.txt"))return;
        std::vector<fs::path> directories;
        for(const auto& item:fs::recursive_directory_iterator(tx)) {
            noLinks(tx,item.path().lexically_relative(tx));
            if(item.is_directory())directories.push_back(item.path());
            else if(item.is_regular_file()){if(item.path()!=tx/"COMMITTED.txt")fs::remove(item.path());}
            else throw std::runtime_error("Unsupported file in synchronization transaction.");
        }
        std::sort(directories.begin(),directories.end(),[](const auto& a,const auto& b){return a.native().size()>b.native().size();});
        for(const auto& path:directories)fs::remove(path);
        // Keep the durable commit marker until all rollback data is gone. A
        // crash during cleanup must never be mistaken for an incomplete apply.
        fs::remove(tx/"COMMITTED.txt");
        fs::remove(tx);
    }
    void saveState() {
        const auto path=internal("sync-state.dat");fs::create_directories(path.parent_path());
        Writer w;w.string("TEAMSYNC-STATE-3");w.string(endpoint);w.string(baseline);
        w.u64(revision);w.u8(pending?1:0);w.string(lastSync);w.string(lastConflictDigest);w.string(lastConflictId);w.string(clientId);
        const auto tmp=path.string()+".new";
        noLinks(root/".teamsync","sync-state.dat.new");
        std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
        if(!out)throw std::runtime_error("Cannot save synchronization state.");
        out.write(w.bytes.data(),static_cast<std::streamsize>(w.bytes.size()));out.close();
        if(!out)throw std::runtime_error("Cannot finish saving synchronization state.");
        const auto previous=path.string()+".previous";std::error_code ec;fs::remove(previous,ec);
        if(fs::exists(path))fs::rename(path,previous);
        try { fs::rename(tmp,path); }
        catch (...) { if(fs::exists(previous))fs::rename(previous,path);throw; }
    }
    void loadState() {
        try {
            auto path=internal("sync-state.dat");
            if(!fs::exists(path)&&fs::exists(path.string()+".previous"))path=path.string()+".previous";
            if(!fs::exists(path)||fs::file_size(path)>16384)return;
            std::ifstream in(path,std::ios::binary);std::vector<char>b(static_cast<std::size_t>(fs::file_size(path)));
            in.read(b.data(),static_cast<std::streamsize>(b.size()));Reader r{b};if(r.string()!="TEAMSYNC-STATE-3")return;
            endpoint=r.string();baseline=r.string();revision=r.u64();pending=r.u8()!=0;lastSync=r.string();
            if(r.p<r.bytes.size()){lastConflictDigest=r.string();lastConflictId=r.string();}
            if(r.p<r.bytes.size())clientId=r.string();
            if(clientId.empty()||clientId.size()>96)throw std::runtime_error("Invalid synchronization client identity.");
            r.end();
        }catch(...){pending=true;}
    }
    void report(const std::string& phase,const std::string& file,std::uint64_t done,std::uint64_t total,bool active=true){TransferProgress value;value.phase=phase;value.file=file;value.transferred=done;value.total=total;value.active=active;value.cancelled=cancelled.load();value.resumable=phase=="Interrupted"||value.cancelled;const auto seconds=std::chrono::duration<double>(Clock::now()-started).count();value.bytesPerSecond=seconds>0?sessionBytes/seconds:0;std::function<void(const TransferProgress&)> handler;{std::lock_guard<std::mutex> l(callbackMutex);progress=value;handler=progressHandler;}if(handler)handler(value);}
    void begin(){cancelled=stopping.load();sessionGeneration=cancellationGeneration.load();sessionBytes=0;cacheScope.fill('\0');started=Clock::now();report("Checking workspace","",0,0);}
    bool isCancelled() const { return cancelled.load()||stopping.load()||sessionGeneration!=cancellationGeneration.load(); }
    void stopTransfers() {
        cancelled=true;++cancellationGeneration;
        const auto active=transferSocket.load();if(active!=-1)stopSocket(static_cast<Socket>(active));
        std::lock_guard<std::mutex> lock(socketsMutex);for(const auto socket:activeSockets)stopSocket(socket);
    }
    TransferProgress progressCopy() const { std::lock_guard<std::mutex> lock(callbackMutex);return progress; }
    std::string preserve(const std::string& reason){const auto hash=digest(scan(root));if(hash==lastConflictDigest&&!lastConflictId.empty())return lastConflictId;lastConflictId=WorkspaceRecovery(root).saveConflict(reason);lastConflictDigest=hash;saveState();return lastConflictId;}
    void restoreOriginal(const fs::path& original,const std::string& relative) {
        if(!safeRelative(relative))throw std::runtime_error("Unsafe rollback destination.");
        noLinks(root,relative);const auto target=root/relative;
        const auto temporary=fs::path(target.string()+".teamsync-rollback-"+token());
        noLinks(root,temporary.lexically_relative(root));
        fs::create_directories(target.parent_path());fs::copy_file(original,temporary);
        if(fs::exists(target)) {
            if(!fs::is_regular_file(target))throw std::runtime_error("Rollback destination is not a regular file.");
            fs::remove(target);
        }
        fs::rename(temporary,target);
    }
    void recoverTransactions() {
        const auto folder=internal("transactions");
        if(!fs::exists(folder))return;
        for(const auto& entry:fs::directory_iterator(folder)) {
            noLinks(folder,entry.path().filename());
            const auto tx=entry.path();
            if(!entry.is_directory())continue;
            if(fs::exists(tx/"COMMITTED.txt")){try{removeCommittedTransaction(tx);}catch(...){}continue;}
            if(!fs::exists(tx/"APPLYING.txt")||fs::exists(tx/"ROLLED-BACK.txt"))continue;
            noLinks(tx,"journal.txt");std::ifstream journal(tx/"journal.txt");
            std::vector<std::string> paths;std::map<std::string,std::string> expectedHashes;std::string path;
            while(std::getline(journal,path)) {
                const auto separator=path.find('\t');std::string expected;
                if(separator!=std::string::npos){expected=path.substr(separator+1);path.resize(separator);}
                if(!safeRelative(path)||(!expected.empty()&&expected!="-"&&(expected.size()!=64||expected.find_first_not_of("0123456789abcdef")!=std::string::npos)))throw std::runtime_error("Unsafe recovery journal; workspace was not changed.");
                paths.push_back(path);expectedHashes[path]=expected;if(paths.size()>maxFiles)throw std::runtime_error("Recovery journal exceeds the file safety limit.");
            }
            if(!journal.eof())throw std::runtime_error("Cannot read interrupted synchronization journal.");
            // A crash may occur between any two renames. Keep the on-disk
            // intermediate state as a recovery copy before restoring originals.
            WorkspaceRecovery(root).saveConflict("Interrupted synchronization was rolled back at startup");
            for(const auto& name:paths) {
                noLinks(root,name);noLinks(tx,fs::path("original")/name);
                const auto original=tx/"original"/name;
                if(fs::exists(original)) { restoreOriginal(original,name); }
                else if(fs::exists(root/name)) {
                    const auto staged=tx/"staged"/name;noLinks(tx,fs::path("staged")/name);
                    // Only remove a newly installed file when it still matches
                    // the interrupted transaction. Unrelated later edits stay.
                    const auto expected=expectedHashes[name];
                    if((!expected.empty()&&expected!="-"&&hashFile(root/name)==expected)||(expected.empty()&&fs::exists(staged)&&hashFile(root/name)==hashFile(staged)))fs::remove(root/name);
                }
            }
            std::ofstream(tx/"ROLLED-BACK.txt")<<timestamp()<<'\n';
        }
    }
    fs::path cache(const Entry& e)const{return internal(fs::path("transfer-cache")/(std::string(cacheScope.data())+e.hash+".part"));}
    std::uint64_t cached(const Entry& e){const auto p=cache(e);fs::create_directories(p.parent_path());if(!fs::exists(p))return 0;const auto n=fs::file_size(p);if(n>e.size){fs::remove(p);return 0;}if(n==e.size&&hashFile(p)!=e.hash){fs::remove(p);return 0;}return n;}
    void writeStatus(Socket s,bool ok,const std::string& text,const Manifest& m){Writer w;w.u8(ok?1:0);w.string(text);w.u64(revision);writeManifest(w,m);sendPacket(s,w.bytes,cancelled);}
    bool readStatus(Socket s,std::string& error,std::uint64_t& rev,Manifest& m){auto b=packet(s,cancelled);Reader r{b};const auto ok=r.u8()!=0;error=r.string();rev=r.u64();m=readManifest(r);r.end();return ok;}
    void transferSend(Socket s,const Manifest& offered){auto b=packet(s,cancelled);Reader r{b};const auto count=r.u32();if(count>offered.size())throw std::runtime_error("Invalid file request count.");std::set<std::string> seen;std::vector<std::pair<Entry,std::uint64_t>> wanted;std::uint64_t total=0;for(std::uint32_t i=0;i<count;++i){const auto path=r.string();const auto offset=r.u64();const auto found=offered.find(path);if(found==offered.end()||offset>found->second.size||!seen.insert(path).second)throw std::runtime_error("Invalid requested workspace file.");wanted.emplace_back(found->second,offset);total+=found->second.size-offset;}r.end();std::uint64_t done=0;std::array<char,chunkSize> buffer{};for(const auto& item:wanted){const auto& e=item.first;noLinks(root,e.path);std::ifstream in(root/e.path,std::ios::binary);if(!in)throw std::runtime_error("A requested workspace file is no longer readable.");in.seekg(static_cast<std::streamoff>(item.second));std::uint64_t position=item.second;report("Sending",e.path,done,total);while(position<e.size){if(isCancelled())throw std::runtime_error("Transfer cancelled; partial files were kept for Retry.");const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(chunkSize,e.size-position));in.read(buffer.data(),static_cast<std::streamsize>(n));if(static_cast<std::size_t>(in.gcount())!=n)throw std::runtime_error("A file changed while sending. Retry synchronization.");std::vector<char> bytes(buffer.data(),buffer.data()+n);sendPacket(s,bytes,cancelled);position+=n;done+=n;sessionBytes+=n;report("Sending",e.path,done,total);}}}
    void transferReceive(Socket s,const Manifest& desired,const Manifest& existing){Writer wanted;std::set<std::string> requestedHashes;std::vector<std::pair<Entry,std::uint64_t>> entries;std::uint64_t total=0;for(const auto& item:desired){const auto local=existing.find(item.first);if(local!=existing.end()&&local->second.hash==item.second.hash&&local->second.size==item.second.size)continue;if(!requestedHashes.insert(item.second.hash).second)continue;const auto offset=cached(item.second);entries.emplace_back(item.second,offset);total+=item.second.size-offset;}wanted.u32(static_cast<std::uint32_t>(entries.size()));for(const auto& e:entries){wanted.string(e.first.path);wanted.u64(e.second);}sendPacket(s,wanted.bytes,cancelled);std::uint64_t done=0;for(const auto& item:entries){const auto& e=item.first;const auto path=cache(e);std::ofstream out(path,std::ios::binary|std::ios::app);if(!out)throw std::runtime_error("Cannot store a partial file. Check free disk space.");auto position=item.second;report("Receiving",e.path,done,total);while(position<e.size){auto b=packet(s,cancelled,chunkSize);if(b.empty()||b.size()>e.size-position)throw std::runtime_error("Invalid file chunk length.");out.write(b.data(),static_cast<std::streamsize>(b.size()));out.flush();if(!out)throw std::runtime_error("Cannot write transferred data. Check free disk space.");position+=b.size();done+=b.size();sessionBytes+=b.size();report("Receiving",e.path,done,total);}out.close();if(hashFile(path,[this]{return isCancelled();})!=e.hash){fs::remove(path);throw std::runtime_error("SHA-256 verification failed. Retry will download that file again.");}}}
    void commit(const Manifest& desired,const Manifest& before,const std::string& expected){if(isCancelled())throw std::runtime_error("Transfer cancelled before installation. Verified partial files were kept.");if(digest(scan(root))!=expected)throw std::runtime_error("Local files changed during transfer. No files were overwritten; Retry after saving your work.");const auto tx=internal(fs::path("transactions")/token());std::vector<std::string> touched;for(const auto& old:before){const auto next=desired.find(old.first);if(next==desired.end()||next->second.hash!=old.second.hash)touched.push_back(old.first);}for(const auto& next:desired)if(before.find(next.first)==before.end())touched.push_back(next.first);if(touched.empty())return;fs::create_directories(tx);for(const auto& path:touched){noLinks(root,path);if(fs::exists(root/path)&&!fs::is_regular_file(root/path))throw std::runtime_error("A managed file destination is not a regular file.");const auto old=before.find(path);if(old!=before.end()){const auto backup=tx/"original"/path;fs::create_directories(backup.parent_path());fs::copy_file(root/path,backup);}const auto next=desired.find(path);if(next!=desired.end()){const auto staged=tx/"staged"/path;fs::create_directories(staged.parent_path());if(!fs::exists(cache(next->second))||hashFile(cache(next->second),[this]{return isCancelled();})!=next->second.hash)throw std::runtime_error("A verified staging file is unavailable. Retry synchronization.");fs::copy_file(cache(next->second),staged);}}
        std::ofstream journal(tx/"journal.txt");
        for(const auto& path:touched){const auto next=desired.find(path);journal<<path<<'\t'<<(next==desired.end()?"-":next->second.hash)<<'\n';}
        journal.close();if(!journal)throw std::runtime_error("Cannot save synchronization journal.");
        std::ofstream applying(tx/"APPLYING.txt");applying<<timestamp()<<'\n';applying.close();
        if(!applying)throw std::runtime_error("Cannot mark prepared synchronization transaction.");
        std::vector<std::string> applied;
        try {
            for(const auto& path:touched) {
                if(isCancelled())throw std::runtime_error("Transfer cancelled; installation was rolled back.");
                noLinks(root,path);const auto target=root/path;fs::create_directories(target.parent_path());applied.push_back(path);
                if(desired.find(path)==desired.end()){fs::remove(target);continue;}
                // The verified staging tree is on the same filesystem. Rename
                // avoids making a third full-size copy of every large file.
                if(fs::exists(target))fs::remove(target);
                fs::rename(tx/"staged"/path,target);
            }
            std::ofstream committed(tx/"COMMITTED.txt");committed<<timestamp()<<'\n';committed.close();
            if(!committed)throw std::runtime_error("Cannot mark synchronization committed.");
        }
        catch(...) {
            const auto failure=std::current_exception();
            for(auto i=applied.rbegin();i!=applied.rend();++i) {
                try{const auto original=tx/"original"/ *i;if(fs::exists(original))restoreOriginal(original,*i);else fs::remove(root/ *i);}
                catch(...){/* Originals and APPLYING journal remain for startup recovery. */}
            }
            std::rethrow_exception(failure);
        }
        // Retained recovery/conflict history is separate. Completed transport
        // staging and content cache are disposable, unlike interrupted parts.
        try{removeCommittedTransaction(tx);}catch(...){}
        std::set<std::string> cleaned;
        for(const auto& path:touched){const auto next=desired.find(path);if(next!=desired.end()&&cleaned.insert(next->second.hash).second){try{const auto complete=cache(next->second);noLinks(root,complete.lexically_relative(root));if(fs::exists(complete))fs::remove(complete);}catch(...){}}}
    }
    void refreshHost(){const auto now=digest(scan(root));if(hostDigest!=now){hostDigest=now;++revision;baseline=now;pending=false;saveState();}}
    bool receiveHost(bool& changed,std::string& error,bool initial=false,bool allowDirty=false) {
        changed=false;begin();
        try {
            Manifest before;std::string beforeHash,requestAddress,requestCode,requestClientId;std::uint16_t requestPort=0;std::uint64_t requestRevision=0;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                before=scan(root);beforeHash=digest(before);
                if(!initial&&!allowDirty&&(pending||(!baseline.empty()&&baseline!=beforeHash))) {
                    pending=true;saveState();throw std::runtime_error("Unsynced local changes are safe on this computer. Sync/upload them before downloading host changes.");
                }
                requestAddress=address;requestCode=code;requestClientId=clientId;requestPort=port;requestRevision=revision;
            }
            SocketGuard s{connectTo(requestAddress,requestPort)};transferSocket=static_cast<std::intptr_t>(s.value);
            Writer request;request.string(protocol);request.string(initial?"join":"pull");request.string(requestCode);request.string(requestClientId);request.u64(requestRevision);writeManifest(request,before);
            sendPacket(s.value,request.bytes,cancelled);std::uint64_t rev=0;Manifest desired;
            if(!readStatus(s.value,error,rev,desired))throw std::runtime_error(error);
            const auto desiredHash=digest(desired);
            if(initial&&beforeHash!=desiredHash&&!before.empty()){std::lock_guard<std::recursive_mutex> lock(mutex);preserve("Joined a hosted workspace; pre-join local files");}
            transferReceive(s.value,desired,before);
            auto final=packet(s.value,cancelled);Reader finish{final};const auto ok=finish.u8()!=0;const auto message=finish.string();finish.end();
            if(!ok)throw std::runtime_error(message);
            {
                std::lock_guard<std::recursive_mutex> lock(mutex);commit(desired,before,beforeHash);
                changed=beforeHash!=desiredHash;revision=rev;baseline=desiredHash;pending=false;endpoint=endpointKey(requestAddress,requestPort,requestCode);lastSync=timestamp();saveState();
            }
            transferSocket=-1;report("Up to date","",sessionBytes,sessionBytes,false);return true;
        }catch(const std::exception& e){
            transferSocket=-1;error=e.what();const auto current=progressCopy();report("Interrupted","",current.transferred,current.total,false);return false;
        }
    }
    void serve(Socket s,const std::string& peer) {
        struct PeerActivity {
            Impl* owner; std::string address; bool authenticated = false;
            ~PeerActivity() {
                if (!authenticated) return;
                std::lock_guard<std::recursive_mutex> lock(owner->mutex);
                --owner->peerActive[address]; owner->peerSeen[address] = Clock::now();
            }
        } activity{this, peer};
        timeout(s);
        {std::lock_guard<std::mutex> lock(socketsMutex);activeSockets.insert(s);}
        struct ActiveSocketGuard {
            Impl& owner;Socket socket;
            ~ActiveSocketGuard(){std::lock_guard<std::mutex> lock(owner.socketsMutex);owner.activeSockets.erase(socket);}
        } activeGuard{*this,s};
        begin();
        try {
            auto b=packet(s,cancelled);Reader r{b};
            const auto version=r.string(),action=r.string(),givenCode=r.string(),remoteClientId=r.string();
            if(remoteClientId.empty()||remoteClientId.size()>96||remoteClientId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid synchronization client identity.");
            Sha256 clientHash;clientHash.add(remoteClientId.data(),remoteClientId.size());
            const auto scope=clientHash.finish().substr(0,24)+"-";
            std::copy(scope.begin(),scope.end(),cacheScope.begin());
            const auto base=r.u64();const auto incoming=readManifest(r);r.end();
            Manifest existing;std::string original;std::uint64_t offeredRevision=0;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                if(version!=protocol){writeStatus(s,false,"Incompatible TeamSync version. Update both computers to the same version.",{});return;}
                if(givenCode!=code){writeStatus(s,false,"Incorrect pairing code.",{});return;}
                if(std::find(peers.begin(),peers.end(),peer)==peers.end())peers.push_back(peer);
                peerSeen[peer]=Clock::now();
                ++peerActive[peer]; activity.authenticated = true;
                refreshHost();existing=scan(root);original=digest(existing);offeredRevision=revision;
                if(action=="status"){writeStatus(s,true,"",existing);report("Up to date","",0,0,false);return;}
                if(action=="push"&&base!=revision){writeStatus(s,false,"Conflict: another computer or the host updated the workspace. Your local changes have not been overwritten.",existing);return;}
                if(action!="pull"&&action!="join"&&action!="push"){writeStatus(s,false,"Unknown TeamSync operation.",{});return;}
                writeStatus(s,true,"",action=="push"?Manifest{}:existing);
            }
            // No workspace lock is held while bytes cross the network. Local
            // work remains usable; the revision is checked again at commit.
            if(action=="pull"||action=="join") {
                transferSend(s,existing);
                std::lock_guard<std::recursive_mutex> lock(mutex);
                const bool stable=digest(scan(root))==original&&revision==offeredRevision;
                Writer final;final.u8(stable?1:0);final.string(stable?"":"Host files changed during transfer. Retry reuses verified partial files.");
                sendPacket(s,final.bytes,cancelled);
            }else{
                transferReceive(s,incoming,existing);
                std::function<void()> handler;
                {
                    std::lock_guard<std::recursive_mutex> lock(mutex);refreshHost();
                    if(revision!=offeredRevision){writeStatus(s,false,"Conflict: host changed during upload. Local work remains pending.",{});return;}
                    try{commit(incoming,existing,original);}
                    catch(const std::exception& e){writeStatus(s,false,e.what(),{});return;}
                    ++revision;hostDigest=digest(incoming);baseline=hostDigest;pending=false;lastSync=timestamp();saveState();writeStatus(s,true,"",{});
                    std::lock_guard<std::mutex> callbacks(callbackMutex);handler=remoteHandler;
                }
                if(handler)handler();
            }
            report("Up to date","",sessionBytes,sessionBytes,false);
        }catch(const std::exception&){const auto current=progressCopy();report("Interrupted","",current.transferred,current.total,false);}
    }
    void loop() {
        const auto reap=[this](bool all) {
            for(auto it=clientWorkers.begin();it!=clientWorkers.end();) {
                if(all||it->done->load()) { if(it->thread.joinable())it->thread.join();it=clientWorkers.erase(it); }
                else ++it;
            }
        };
        while(!stopping) {
            reap(false);fd_set ready;FD_ZERO(&ready);FD_SET(server,&ready);Socket highest=server;
            if(discovery!=invalidSocket){FD_SET(discovery,&ready);highest=std::max(highest,discovery);}
            timeval wait{0,150000};
            if(select(static_cast<int>(highest+1),&ready,nullptr,nullptr,&wait)<=0)continue;
            if(discovery!=invalidSocket&&FD_ISSET(discovery,&ready)) {
                char query[128]{};sockaddr_in peer{};socklen_t n=sizeof(peer);
                const auto size=recvfrom(discovery,query,sizeof(query),0,reinterpret_cast<sockaddr*>(&peer),&n);
                if(size==static_cast<int>(std::strlen("TEAMSYNC_DISCOVER_3"))&&std::string(query,static_cast<std::size_t>(size))=="TEAMSYNC_DISCOVER_3") {
                    char name[128]{};gethostname(name,sizeof(name)-1);
                    const auto response=std::string("TEAMSYNC_HOST_3\n")+name+"\n"+std::to_string(port);
                    sendto(discovery,response.data(),static_cast<int>(response.size()),0,reinterpret_cast<sockaddr*>(&peer),n);
                }
            }
            if(FD_ISSET(server,&ready)) {
                sockaddr_in peer{};socklen_t n=sizeof(peer);
                SocketGuard accepted{accept(server,reinterpret_cast<sockaddr*>(&peer),&n)};
                if(accepted.value==invalidSocket)continue;
                // Bounded workers prevent a stalled/malicious peer exhausting
                // threads, while an ordinary large transfer no longer blocks
                // discovery or another computer's status/update requests.
                if(clientWorkers.size()>=8)continue;
                char addressText[INET_ADDRSTRLEN]{};inet_ntop(AF_INET,&peer.sin_addr,addressText,sizeof(addressText));
                const auto socket=accepted.value;const std::string address=addressText;
                const auto done=std::make_shared<std::atomic_bool>(false);
                std::thread thread([this,socket,address,done] {
                    {SocketGuard guard{socket};serve(socket,address);}
                    *done=true;
                });
                accepted.value=invalidSocket;clientWorkers.push_back({std::move(thread),done});
            }
        }
        reap(true);
    }
};

thread_local Clock::time_point ConsoleLanSync::Impl::started{};
thread_local std::uint64_t ConsoleLanSync::Impl::sessionBytes=0;
thread_local std::uint64_t ConsoleLanSync::Impl::sessionGeneration=0;
thread_local std::array<char, 26> ConsoleLanSync::Impl::cacheScope{};

ConsoleLanSync::ConsoleLanSync(fs::path root):impl_(new Impl(std::move(root))){}
ConsoleLanSync::~ConsoleLanSync(){disconnect();}
bool ConsoleLanSync::host(std::uint16_t port,std::string& code,std::string& error,const std::string& requested){auto& p=*impl_;std::lock_guard<std::recursive_mutex> l(p.mutex);if(p.mode){error="A workspace connection is already active.";return false;}try{SocketGuard server{::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)};if(server.value==invalidSocket)throw std::runtime_error("Cannot create host socket.");int reuse=1;setsockopt(server.value,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&reuse),sizeof(reuse));sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_ANY);a.sin_port=htons(port);if(bind(server.value,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||listen(server.value,16)!=0)throw std::runtime_error("Cannot host on that port. Choose another port and allow TeamSync on Private networks.");socklen_t n=sizeof(a);getsockname(server.value,reinterpret_cast<sockaddr*>(&a),&n);p.port=ntohs(a.sin_port);p.code=requested.empty()?std::to_string(100000+std::random_device{}()%900000):requested;if(p.code.size()>128)throw std::runtime_error("Pairing code is too long.");p.server=server.value;server.value=invalidSocket;p.discovery=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(p.discovery!=invalidSocket){setsockopt(p.discovery,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&reuse),sizeof(reuse));a.sin_port=htons(discoveryPort);if(bind(p.discovery,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0){closeSocket(p.discovery);p.discovery=invalidSocket;}}p.revision=std::max<std::uint64_t>(p.revision,1);p.hostDigest=digest(scan(p.root));p.baseline=p.hostDigest;p.pending=false;p.endpoint="host:"+std::to_string(p.port);p.saveState();p.stopping=false;p.mode=1;code=p.code;p.worker=std::thread([&p]{p.loop();});return true;}catch(const std::exception& e){closeSocket(p.server);closeSocket(p.discovery);p.server=p.discovery=invalidSocket;error=e.what();return false;}}
bool ConsoleLanSync::join(const std::string& address,std::uint16_t port,const std::string& code,std::string& error) {
    auto& p=*impl_;std::unique_lock<std::mutex> operation(p.operationMutex,std::try_to_lock);
    if(!operation.owns_lock()){error="Another synchronization is running.";return false;}
    bool queued=false,reconnect=false;std::uint64_t base=0;
    try {
        {
            std::lock_guard<std::recursive_mutex> lock(p.mutex);
            if(p.mode){error="A workspace connection is already active.";return false;}
            p.loadState(); // A previous engine/process may have resumed this folder.
            p.address=address;p.port=port;p.code=code;p.stopping=false;
            const auto target=endpointKey(address,port,code);
            reconnect=p.endpoint==target&&!p.baseline.empty();
            queued=reconnect&&(p.pending||digest(scan(p.root))!=p.baseline);
            base=p.revision;
        }
        if(queued) {
            p.begin();SocketGuard s{connectTo(address,port)};p.transferSocket=static_cast<std::intptr_t>(s.value);
            Writer w;w.string(protocol);w.string("status");w.string(code);w.string(p.clientId);w.u64(base);writeManifest(w,{});
            sendPacket(s.value,w.bytes,p.cancelled);std::uint64_t revision=0;Manifest m;
            if(!p.readStatus(s.value,error,revision,m))throw std::runtime_error(error);
            p.transferSocket=-1;
            {std::lock_guard<std::recursive_mutex> lock(p.mutex);p.pending=true;p.mode=2;p.saveState();}
            p.report("Local changes queued","",0,0,false);return true;
        }
        bool changed=false;if(!p.receiveHost(changed,error,!reconnect))throw std::runtime_error(error);p.mode=2;return true;
    }catch(const std::exception& e){
        std::lock_guard<std::recursive_mutex> lock(p.mutex);p.transferSocket=-1;p.port=0;p.address.clear();p.code.clear();error=e.what();return false;
    }
}
void ConsoleLanSync::disconnect() {
    auto& p=*impl_;p.stopping=true;p.stopTransfers();
    const auto active=p.transferSocket.load();if(active!=-1)stopSocket(static_cast<Socket>(active));
    if(p.worker.joinable())p.worker.join();
    std::lock_guard<std::mutex> operation(p.operationMutex);
    std::lock_guard<std::recursive_mutex> lock(p.mutex);
    closeSocket(p.server);closeSocket(p.discovery);p.server=p.discovery=invalidSocket;
    p.mode=0;p.peers.clear();p.peerSeen.clear();p.peerActive.clear();p.address.clear();p.code.clear();p.port=0;
}
bool ConsoleLanSync::pull(bool& changed,std::string& error) {
    auto& p=*impl_;std::unique_lock<std::mutex> operation(p.operationMutex,std::try_to_lock);
    changed=false;if(!operation.owns_lock()){error="Another synchronization is running.";return false;}
    if(p.mode!=2)return true;
    return p.receiveHost(changed,error);
}
bool ConsoleLanSync::push(std::string& error) {
    auto& p=*impl_;std::unique_lock<std::mutex> operation(p.operationMutex,std::try_to_lock);
    if(!operation.owns_lock()){error="Another synchronization is running.";return false;}
    {
        std::lock_guard<std::recursive_mutex> lock(p.mutex);
        if(p.mode==1){try{p.refreshHost();return true;}catch(const std::exception& e){error=e.what();return false;}}
        if(p.mode!=2)return true;
    }
    p.begin();
    try {
        Manifest offered;std::string offeredHash,address,code,clientId;std::uint16_t port=0;std::uint64_t base=0;
        {
            std::lock_guard<std::recursive_mutex> lock(p.mutex);
            offered=scan(p.root);offeredHash=digest(offered);
            if(!p.pending&&offeredHash==p.baseline){p.report("Up to date","",0,0,false);return true;}
            p.pending=true;p.saveState();address=p.address;port=p.port;code=p.code;base=p.revision;clientId=p.clientId;
        }
        SocketGuard s{connectTo(address,port)};p.transferSocket=static_cast<std::intptr_t>(s.value);
        Writer w;w.string(protocol);w.string("push");w.string(code);w.string(clientId);w.u64(base);writeManifest(w,offered);sendPacket(s.value,w.bytes,p.cancelled);
        std::uint64_t rev=0;Manifest m;
        if(!p.readStatus(s.value,error,rev,m)){
            std::lock_guard<std::recursive_mutex> lock(p.mutex);
            const auto saved=p.preserve(error);throw std::runtime_error(error+" Local recovery copy: "+saved);
        }
        p.transferSend(s.value,offered);
        if(!p.readStatus(s.value,error,rev,m)){
            std::lock_guard<std::recursive_mutex> lock(p.mutex);
            const auto saved=p.preserve(error);throw std::runtime_error(error+" Local recovery copy: "+saved);
        }
        {
            std::lock_guard<std::recursive_mutex> lock(p.mutex);
            p.revision=rev;p.baseline=offeredHash;p.pending=digest(scan(p.root))!=offeredHash;p.lastSync=timestamp();p.saveState();
        }
        p.transferSocket=-1;p.report("Up to date","",p.sessionBytes,p.sessionBytes,false);return true;
    }catch(const std::exception& e){
        p.transferSocket=-1;error=e.what();const auto current=p.progressCopy();p.report("Interrupted","",current.transferred,current.total,false);return false;
    }
}
bool ConsoleLanSync::sync(bool& changed,std::string& error){changed=false;if(!push(error))return false;return pull(changed,error);}
void ConsoleLanSync::executeLocalMutation(const std::function<void()>& action){std::lock_guard<std::recursive_mutex> l(impl_->mutex);action();}
void ConsoleLanSync::withWorkspaceLock(const std::function<void()>& action){executeLocalMutation(action);}
bool ConsoleLanSync::adoptHostAfterPreservingLocal(bool& changed,std::string& error) {
    auto& p=*impl_;std::unique_lock<std::mutex> operation(p.operationMutex,std::try_to_lock);
    if(!operation.owns_lock()){error="Another synchronization is running.";return false;}
    if(p.mode!=2){error="Join a host before resolving a synchronization conflict.";return false;}
    try {
        {std::lock_guard<std::recursive_mutex> lock(p.mutex);p.preserve("User selected host version after keeping local recovery copy");}
        return p.receiveHost(changed,error,false,true);
    }catch(const std::exception& e){error=e.what();return false;}
}
void ConsoleLanSync::noteLocalChange(){auto& p=*impl_;std::lock_guard<std::recursive_mutex> l(p.mutex);p.pending=true;try{if(p.mode==1)p.refreshHost();else p.saveState();}catch(...){}}
bool ConsoleLanSync::hasPendingChanges()const{auto& p=*impl_;std::lock_guard<std::recursive_mutex> l(p.mutex);if(p.pending)return true;try{return p.mode!=1&&!p.baseline.empty()&&digest(scan(p.root))!=p.baseline;}catch(...){return true;}}
bool ConsoleLanSync::isActive()const noexcept{return impl_->mode!=0;}
bool ConsoleLanSync::isHost()const noexcept{return impl_->mode==1;}
std::uint64_t ConsoleLanSync::revision()const{std::lock_guard<std::recursive_mutex>l(impl_->mutex);return impl_->revision;}
std::uint16_t ConsoleLanSync::port()const{std::lock_guard<std::recursive_mutex>l(impl_->mutex);return impl_->port;}
std::string ConsoleLanSync::hostAddress()const{std::lock_guard<std::recursive_mutex>l(impl_->mutex);return impl_->address;}
std::string ConsoleLanSync::pairingCode()const{std::lock_guard<std::recursive_mutex>l(impl_->mutex);return impl_->code;}
std::string ConsoleLanSync::lastSyncText()const{std::lock_guard<std::recursive_mutex>l(impl_->mutex);return impl_->lastSync;}
std::vector<std::string> ConsoleLanSync::peerAddresses()const {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    std::vector<std::string> result;
    for (const auto& entry : impl_->peerSeen) {
        const auto active = impl_->peerActive.find(entry.first);
        if (Clock::now() - entry.second < std::chrono::seconds(8) ||
            (active != impl_->peerActive.end() && active->second > 0)) result.push_back(entry.first);
    }
    return result;
}
std::string ConsoleLanSync::status()const{auto& p=*impl_;std::lock_guard<std::recursive_mutex>l(p.mutex);if(p.mode==1)return "Hosting on port "+std::to_string(p.port)+" | Pairing code "+p.code;if(p.mode==2)return "Connected to "+p.address+":"+std::to_string(p.port)+(p.pending?" | Local changes waiting to sync":"");return "Local workspace (not shared on the network).";}
std::string ConsoleLanSync::localAddresses()const{char name[256]{};if(gethostname(name,sizeof(name)-1)!=0)return "this computer";addrinfo hints{};hints.ai_family=AF_INET;addrinfo* result=nullptr;if(getaddrinfo(name,nullptr,&hints,&result)!=0)return "this computer";std::set<std::string> addresses;for(auto* item=result;item;item=item->ai_next){char text[INET_ADDRSTRLEN]{};auto* a=reinterpret_cast<sockaddr_in*>(item->ai_addr);if(inet_ntop(AF_INET,&a->sin_addr,text,sizeof(text)))addresses.insert(text);}freeaddrinfo(result);std::string text;for(const auto& a:addresses){if(!text.empty())text+=", ";text+=a;}return text.empty()?"this computer":text;}
std::uint64_t ConsoleLanSync::fingerprint()const{auto& p=*impl_;std::lock_guard<std::recursive_mutex>l(p.mutex);const auto h=digest(scan(p.root));return std::stoull(h.substr(0,16),nullptr,16);}
void ConsoleLanSync::setRemoteChangeHandler(std::function<void()> handler){std::lock_guard<std::mutex>l(impl_->callbackMutex);impl_->remoteHandler=std::move(handler);}
void ConsoleLanSync::setProgressHandler(std::function<void(const TransferProgress&)> handler){std::lock_guard<std::mutex>l(impl_->callbackMutex);impl_->progressHandler=std::move(handler);}
ConsoleLanSync::TransferProgress ConsoleLanSync::progress()const{std::lock_guard<std::mutex>l(impl_->callbackMutex);return impl_->progress;}
void ConsoleLanSync::cancelTransfer(){impl_->stopTransfers();}
std::vector<ConsoleLanSync::NearbyWorkspace> ConsoleLanSync::discoverNearby(int timeoutMs){(void)socketRuntime();std::vector<NearbyWorkspace> result;SocketGuard s{::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)};if(s.value==invalidSocket)return result;int yes=1;setsockopt(s.value,SOL_SOCKET,SO_BROADCAST,reinterpret_cast<const char*>(&yes),sizeof(yes));sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_ANY);if(bind(s.value,reinterpret_cast<sockaddr*>(&local),sizeof(local))!=0)return result;const char query[]="TEAMSYNC_DISCOVER_3";for(const char* address:{"255.255.255.255","127.0.0.1"}){sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(discoveryPort);inet_pton(AF_INET,address,&target.sin_addr);sendto(s.value,query,sizeof(query)-1,0,reinterpret_cast<sockaddr*>(&target),sizeof(target));}const auto until=Clock::now()+std::chrono::milliseconds(std::max(50,std::min(timeoutMs,10000)));std::set<std::string> seen;while(Clock::now()<until){fd_set read;FD_ZERO(&read);FD_SET(s.value,&read);timeval wait{0,100000};if(select(static_cast<int>(s.value+1),&read,nullptr,nullptr,&wait)<=0)continue;char b[512]{};sockaddr_in peer{};socklen_t size=sizeof(peer);const auto n=recvfrom(s.value,b,sizeof(b),0,reinterpret_cast<sockaddr*>(&peer),&size);if(n<=0)continue;std::istringstream in(std::string(b,static_cast<std::size_t>(n)));std::string signature,name,portText;std::getline(in,signature);std::getline(in,name);std::getline(in,portText);if(signature!="TEAMSYNC_HOST_3"||name.size()>128)continue;try{const auto port=std::stoul(portText);if(!port||port>65535)continue;char address[INET_ADDRSTRLEN]{};inet_ntop(AF_INET,&peer.sin_addr,address,sizeof(address));const auto key=std::string(address)+":"+portText;if(seen.insert(key).second)result.push_back({name,address,static_cast<std::uint16_t>(port)});}catch(...){}}return result;}

} // namespace teamsync
