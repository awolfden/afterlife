#include "network.h"
#include "write-budget.h"
#include "packet-queue.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_tls.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mbedtls/ssl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <new>
#if CONFIG_ESP_WIFI_NVS_ENABLED || CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE || CONFIG_ESP_PHY_INIT_DATA_IN_PARTITION || CONFIG_LWIP_DHCP_RESTORE_LAST_IP
#error "LAN builds must not persist Wi-Fi, PHY or DHCP state"
#endif
#if CONFIG_ESP_TLS_INSECURE || CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY || CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
#error "No insecure verification or TLS1.3 fallback is permitted"
#endif
#if !CONFIG_ESP_TLS_PSK_VERIFICATION || !CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_PSK || !CONFIG_MBEDTLS_CHACHAPOLY_C || !CONFIG_MBEDTLS_SSL_PROTO_TLS1_2
#error "Required ECDHE-PSK/ChaChaPoly TLS1.2 configuration missing"
#endif
namespace watcher_lan {
namespace {
int64_t now(){return esp_timer_get_time()/1000;}
struct Guard {portMUX_TYPE* lock;explicit Guard(portMUX_TYPE& value):lock(&value){portENTER_CRITICAL(lock);}~Guard(){portEXIT_CRITICAL(lock);}};
struct Scrub {void* data;size_t size;~Scrub(){wipe(data,size);}};
using Ring=PacketQueue;
constexpr int suites[]={0xccac,0};
bool want(int result){return result==ESP_TLS_ERR_SSL_WANT_READ||result==ESP_TLS_ERR_SSL_WANT_WRITE;}
}
struct Network::Impl {
 mutable portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
 Credentials pending,current;
 bool has_pending=false,closing=false,stopped=false,failed=false,drop_requested=false;
 uint32_t requested=0,active=0,next_generation=0;
 Ring rx,tx;
 std::atomic<bool> ip{false},connect_wifi{false};
 esp_netif_t* netif=nullptr;bool wifi_init=false,wifi_started=false,own_events=false;
 esp_event_handler_instance_t wifi_handler=nullptr,ip_handler=nullptr;
 esp_tls_t* tls=nullptr;esp_tls_cfg_t tls_cfg{};psk_hint_key_t psk{};
 uint32_t revision=0,generation=0;bool authenticated=false;
 Packet outgoing{};size_t written=0,used=0;char line[192]{};
 int64_t deadline=0,retry_at=0,wifi_retry_at=0;unsigned retry=0;
 static void event(void* arg,esp_event_base_t base,int32_t id,void*){
  auto& self=*static_cast<Impl*>(arg);
  if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP)self.ip.store(true);
  if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){self.ip.store(false);self.connect_wifi.store(true);}
 }
 void revoke(){Guard guard(mutex);active=0;rx.clear();tx.clear();}
 void disconnect(){
  revoke();if(tls){esp_tls_conn_destroy(tls);tls=nullptr;}
  authenticated=false;generation=0;used=written=0;wipe(line,sizeof line);wipe(&outgoing,sizeof outgoing);
  constexpr int delays[]={500,1000,2000,5000};retry_at=now()+delays[retry<3?retry++:3];
 }
 bool teardown(){
  disconnect();ip.store(false);connect_wifi.store(false);
  // Keep state flags until each operation acknowledges cleanup. A failure holds
  // chooser shutdown; never start a second Wi-Fi instance over uncertain cleanup.
  if(wifi_started){if(esp_wifi_stop()!=ESP_OK)return false;wifi_started=false;}
  if(wifi_init){if(esp_wifi_deinit()!=ESP_OK)return false;wifi_init=false;}
  if(wifi_handler){if(esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler)!=ESP_OK)return false;wifi_handler=nullptr;}
  if(ip_handler){if(esp_event_handler_instance_unregister(IP_EVENT,IP_EVENT_STA_GOT_IP,ip_handler)!=ESP_OK)return false;ip_handler=nullptr;}
  if(netif){esp_netif_destroy_default_wifi(netif);netif=nullptr;}
  if(own_events){if(esp_event_loop_delete_default()!=ESP_OK)return false;own_events=false;}
  current.clear();return true;
 }
 bool configure(){
  // Network SDK logs can contain SSIDs; protocol diagnostics remain explicit and secret-free.
  esp_log_level_set("*",ESP_LOG_NONE);
  if(esp_netif_init()!=ESP_OK)return false;
  const auto events=esp_event_loop_create_default();
  if(events!=ESP_OK&&events!=ESP_ERR_INVALID_STATE)return false;
  own_events=events==ESP_OK;
  netif=esp_netif_create_default_wifi_sta();if(!netif)return false;
  wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();init.nvs_enable=0;
  if(esp_wifi_init(&init)!=ESP_OK)return false;
  wifi_init=true;
  if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK||esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK)return false;
  if(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,this,&wifi_handler)!=ESP_OK||
     esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,this,&ip_handler)!=ESP_OK)return false;
  wifi_config_t config{};
  std::memcpy(config.sta.ssid,current.ssid,std::strlen(current.ssid));
  std::memcpy(config.sta.password,current.password,std::strlen(current.password));
  config.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;config.sta.pmf_cfg.capable=true;
  const auto configured=esp_wifi_set_config(WIFI_IF_STA,&config);wipe(&config,sizeof config);
  if(configured!=ESP_OK||esp_wifi_start()!=ESP_OK)return false;
  wifi_started=true;ip.store(false);connect_wifi.store(true);wifi_retry_at=0;retry_at=0;retry=0;return true;
 }
 bool begin_tls(){
  bool supported=false;for(const int* c=esp_tls_get_ciphersuites_list();c&&*c;c++)if(*c==suites[0])supported=true;
  if(!supported)return false;
  tls=esp_tls_init();if(!tls)return false;
  psk={};psk.key=current.key;psk.key_size=32;psk.hint=current.identity;
  tls_cfg={};tls_cfg.psk_hint_key=&psk;tls_cfg.ciphersuites_list=suites;
  tls_cfg.tls_version=ESP_TLS_VER_TLS_1_2;tls_cfg.addr_family=ESP_TLS_AF_INET;
  tls_cfg.non_block=true;tls_cfg.timeout_ms=1500; // Pinned SDK loses fd_sets after first timeout; allow a realistic first TCP window.
  // Only this owner can block here. This is NOT a hard task execution/cleanup bound.
  deadline=now()+3000;return true;
 }
 bool read_lines(){
  char bytes[512];Scrub scrub{bytes,sizeof bytes};const int n=esp_tls_conn_read(tls,bytes,sizeof bytes);
  if(want(n))return true;
  if(n<=0)return false;
  const int64_t at=now();
  for(int i=0;i<n;i++){
   const unsigned char c=bytes[i];
   if(c=='\n'){
    if(used){Packet packet;packet.generation=generation;packet.at=at;packet.size=used;std::memcpy(packet.bytes,line,used);
     Guard guard(mutex);const bool accepted=active==generation&&rx.push(packet);wipe(&packet,sizeof packet);wipe(line,sizeof line);
     if(!accepted)return false;}
    used=0;
   }else if(c=='\r'){}else if(c<32||c>126||used==191)return false;
   else line[used++]=static_cast<char>(c);
  }
  return true;
 }
 bool write_pending(){
  return drain_writes([this](){
   {Guard guard(mutex);if(!active||active!=generation||closing||drop_requested)return WriteStep::failed;
    if(!outgoing.size){if(!tx.pop(outgoing))return WriteStep::idle;written=0;}}
   if(outgoing.generation!=generation||now()-outgoing.at>250)return WriteStep::failed;
   const int n=esp_tls_conn_write(tls,outgoing.bytes+written,outgoing.size-written);
   if(want(n))return WriteStep::blocked; // Exact pointer/length retained; no tight WANT loop.
   if(n<=0||static_cast<size_t>(n)>outgoing.size-written)return WriteStep::failed;
   written+=n;if(written==outgoing.size){wipe(&outgoing,sizeof outgoing);written=0;}
   return WriteStep::progress;
  });
 }
 void run(){
  bool configured=false;
  for(;;){
   Credentials replacement;bool replace=false,quit=false,drop=false;
   {Guard guard(mutex);quit=closing;drop=drop_requested;drop_requested=false;
    if(has_pending){replacement.copy_from(pending);pending.clear();has_pending=false;replace=true;revision=requested;}}
   if(quit){if(teardown()){Guard guard(mutex);pending.clear();stopped=true;break;}Guard guard(mutex);failed=true;}
   else if(replace){
    if(!teardown()){Guard guard(mutex);failed=true;closing=true;}
    else {current.copy_from(replacement);configured=configure();if(!configured){Guard guard(mutex);failed=true;closing=true;}}
   }else if(configured){
    if(drop){retry=0;disconnect();}else if(tls&&!ip.load())disconnect();
    if(!ip.load()){
     if(connect_wifi.load()&&now()>=wifi_retry_at){connect_wifi.store(false);if(esp_wifi_connect()!=ESP_OK)connect_wifi.store(true);wifi_retry_at=now()+3000;}
    }else if(!tls&&now()>=retry_at){if(!begin_tls())disconnect();}
    if(tls){
     if(!authenticated){
      const int result=now()>deadline?-1:esp_tls_conn_new_async(current.address,std::strlen(current.address),current.port,&tls_cfg,tls);
      if(result<0||now()>deadline)disconnect();
      else if(result==1){
       auto* context=static_cast<mbedtls_ssl_context*>(esp_tls_get_ssl_context(tls));
       const char* name=context?mbedtls_ssl_get_ciphersuite(context):nullptr;
       if(!name||mbedtls_ssl_get_ciphersuite_id(name)!=suites[0]||std::strcmp(mbedtls_ssl_get_version(context),"TLSv1.2"))disconnect();
       else {Guard guard(mutex);if(requested==revision&&!closing&&next_generation!=UINT32_MAX){generation=++next_generation;active=generation;authenticated=true;retry=0;}else drop_requested=true;}
      }
     }else if(!read_lines()||!write_pending())disconnect();
    }
   }
   vTaskDelay(pdMS_TO_TICKS(10)>0?pdMS_TO_TICKS(10):1);
  }
 }
 static void task(void* value){static_cast<Impl*>(value)->run();vTaskDelete(nullptr);}
};
bool Network::start(){
 if(impl_)return false;
 auto* value=new(std::nothrow) Impl;if(!value)return false;
 if(xTaskCreate(Impl::task,"watcher_lan",12288,value,1,nullptr)!=pdPASS){delete value;return false;}
 impl_=value;return true; // One allocation per boot; retain object through task shutdown.
}
bool Network::submit(const Credentials& credentials){
 if(!impl_)return false;
 Guard guard(impl_->mutex);
 if(impl_->closing||impl_->failed||impl_->has_pending||impl_->requested==UINT32_MAX)return false;
 impl_->pending.copy_from(credentials);impl_->has_pending=true;impl_->requested++;
 impl_->active=0;impl_->rx.clear();impl_->tx.clear();impl_->drop_requested=true;return true;
}
uint32_t Network::connection() const {if(!impl_)return 0;Guard guard(impl_->mutex);return impl_->active;}
void Network::drop(uint32_t generation){if(!impl_)return;Guard guard(impl_->mutex);if(generation&&generation==impl_->active){impl_->active=0;impl_->rx.clear();impl_->tx.clear();impl_->drop_requested=true;}}
bool Network::send(uint32_t generation,const char* bytes,size_t size){
 if(!impl_||!bytes||!size||size>192)return false;
 Packet packet;packet.generation=generation;packet.at=now();packet.size=size;std::memcpy(packet.bytes,bytes,size);
 Guard guard(impl_->mutex);if(!generation||impl_->active!=generation||impl_->closing){wipe(&packet,sizeof packet);return false;}
 const bool accepted=impl_->tx.push(packet);wipe(&packet,sizeof packet);
 if(!accepted){impl_->active=0;impl_->drop_requested=true;return false;}return true;
}
bool Network::send_batch(uint32_t generation,const char* bytes,size_t size){
 size_t ends[4];if(!impl_||!PacketQueue::layout(bytes,size,ends))return false;
 Guard guard(impl_->mutex);
 if(!generation||impl_->active!=generation||impl_->closing)return false;
 if(impl_->tx.batch(generation,now(),bytes,size))return true;
 // Same overflow policy as send(), without admitting a partial batch.
 impl_->active=0;impl_->drop_requested=true;return false;
}
bool Network::receive(Packet& packet){if(!impl_)return false;Guard guard(impl_->mutex);return impl_->rx.pop(packet);}
bool Network::stop(){if(!impl_)return true;Guard guard(impl_->mutex);impl_->closing=true;impl_->active=0;return impl_->stopped;}
bool Network::fault() const {if(!impl_)return true;Guard guard(impl_->mutex);return impl_->failed;}
bool Network::has_ip() const {return impl_&&impl_->ip.load();}
}
