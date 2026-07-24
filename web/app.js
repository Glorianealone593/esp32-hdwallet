/* ============================================================================
 * DibaVault web UI  —  (c) dibachain
 * Vanilla JS single-page app. No frameworks, no external resources.
 * Served embedded/gzipped from ESP32 flash.
 *
 * Sections: first-run setup wizard, unlock, dashboard
 * (networks / accounts / send / offline signing / settings).
 * ==========================================================================*/
(function () {
"use strict";

/* ===========================================================================
 * 1) QR CODE ENCODER  (compact, dependency-free, byte mode, EC level M)
 *    Correct implementation: GF(256) Reed-Solomon, all 8 masks w/ penalty
 *    scoring, versions 1..10. Enough for addresses & short signed payloads.
 *    Adapted from the public-domain algorithm (Nayuki-style), rewritten small.
 * ==========================================================================*/
var QR = (function () {
  // [ecPerBlock, numBlocksG1, dataCwG1, numBlocksG2, dataCwG2] for EC level M
  var EB = {
    1:[10,1,16,0,0], 2:[16,1,28,0,0], 3:[26,1,44,0,0], 4:[18,2,32,0,0],
    5:[24,2,43,0,0], 6:[16,4,27,0,0], 7:[18,4,31,0,0], 8:[22,2,38,2,39],
    9:[22,3,36,2,37], 10:[26,4,43,1,44]
  };
  // Total data codewords per version (EC level M)
  var CAP = [16,28,44,64,86,108,124,154,182,216];
  // Alignment pattern coordinates per version (empty for v1)
  var ALIGN = {
    1:[], 2:[6,18], 3:[6,22], 4:[6,26], 5:[6,30],
    6:[6,34], 7:[6,22,38], 8:[6,24,42], 9:[6,26,46], 10:[6,28,50]
  };

  function utf8(str){
    var out=[];
    for(var i=0;i<str.length;i++){
      var c=str.charCodeAt(i);
      if(c<0x80)out.push(c);
      else if(c<0x800)out.push(0xC0|c>>6,0x80|c&63);
      else if(c<0xD800||c>=0xE000)out.push(0xE0|c>>12,0x80|(c>>6)&63,0x80|c&63);
      else{ i++; var cp=0x10000+((c&0x3FF)<<10)+(str.charCodeAt(i)&0x3FF);
        out.push(0xF0|cp>>18,0x80|(cp>>12)&63,0x80|(cp>>6)&63,0x80|cp&63); }
    }
    return out;
  }

  // --- Reed-Solomon over GF(256), primitive poly 0x11D ---
  function rsMul(x,y){ var z=0; for(var i=7;i>=0;i--){ z=(z<<1)^((z>>>7)*0x11D); z^=((y>>>i)&1)*x; } return z&0xFF; }
  function rsDivisor(deg){
    var r=new Array(deg).fill(0); r[deg-1]=1; var root=1;
    for(var i=0;i<deg;i++){
      for(var j=0;j<r.length;j++){ r[j]=rsMul(r[j],root); if(j+1<r.length)r[j]^=r[j+1]; }
      root=rsMul(root,2);
    }
    return r;
  }
  function rsRem(data,div){
    var r=new Array(div.length).fill(0);
    for(var k=0;k<data.length;k++){
      var factor=data[k]^r.shift(); r.push(0);
      for(var i=0;i<div.length;i++) r[i]^=rsMul(div[i],factor);
    }
    return r;
  }

  function getBit(x,i){ return ((x>>>i)&1)!==0; }

  // Build the module matrix for `text`; returns 2D bool array or null if too big.
  function build(text){
    var data=utf8(text);
    var ver=0;
    for(var v=1;v<=10;v++){
      var cc=(v<=9)?8:16;
      if(4+cc+8*data.length <= CAP[v-1]*8){ ver=v; break; }
    }
    if(!ver) return null; // payload exceeds v10 capacity

    // ---- bitstream -> data codewords ----
    var bb=[];
    function put(val,len){ for(var i=len-1;i>=0;i--) bb.push((val>>>i)&1); }
    put(4,4);                              // byte mode
    put(data.length,(ver<=9)?8:16);        // char count
    for(var i=0;i<data.length;i++) put(data[i],8);
    var capBits=CAP[ver-1]*8;
    put(0,Math.min(4,capBits-bb.length));  // terminator
    while(bb.length%8) bb.push(0);         // pad to byte boundary
    var cw=[]; for(i=0;i<bb.length;i+=8){ var b=0; for(var j=0;j<8;j++) b=(b<<1)|bb[i+j]; cw.push(b); }
    var pad=[0xEC,0x11],pi=0; while(cw.length<CAP[ver-1]) cw.push(pad[pi++%2]);

    // ---- split into blocks, compute EC, interleave ----
    var t=EB[ver], ecLen=t[0], g1=t[1], d1=t[2], g2=t[3], d2=t[4];
    var div=rsDivisor(ecLen), blocks=[], k=0;
    var total=g1+g2;
    for(i=0;i<total;i++){
      var dl=(i<g1)?d1:d2, dat=cw.slice(k,k+dl); k+=dl;
      blocks.push({d:dat, e:rsRem(dat,div)});
    }
    var out=[], maxD=Math.max(d1,d2);
    for(i=0;i<maxD;i++) for(var bI=0;bI<blocks.length;bI++) if(i<blocks[bI].d.length) out.push(blocks[bI].d[i]);
    for(i=0;i<ecLen;i++) for(bI=0;bI<blocks.length;bI++) out.push(blocks[bI].e[i]);

    // ---- matrix ----
    var size=ver*4+17;
    var m=[], fn=[];
    for(i=0;i<size;i++){ m.push(new Array(size).fill(0)); fn.push(new Array(size).fill(false)); }
    function set(x,y,dark){ m[y][x]=dark?1:0; fn[y][x]=true; }

    // timing
    for(i=0;i<size;i++){ set(6,i,i%2===0); set(i,6,i%2===0); }
    // finders
    function finder(cx,cy){
      for(var dy=-4;dy<=4;dy++)for(var dx=-4;dx<=4;dx++){
        var xx=cx+dx,yy=cy+dy; if(xx<0||xx>=size||yy<0||yy>=size)continue;
        var dist=Math.max(Math.abs(dx),Math.abs(dy)); set(xx,yy,dist!==2&&dist!==4);
      }
    }
    finder(3,3); finder(size-4,3); finder(3,size-4);
    // alignment
    var ap=ALIGN[ver], n=ap.length;
    for(i=0;i<n;i++)for(j=0;j<n;j++){
      if((i===0&&j===0)||(i===0&&j===n-1)||(i===n-1&&j===0))continue;
      var ax=ap[i],ay=ap[j];
      for(dy=-2;dy<=2;dy++)for(dx=-2;dx<=2;dx++) set(ax+dx,ay+dy,Math.max(Math.abs(dx),Math.abs(dy))!==1);
    }
    // reserve format + version areas (drawn for real after masking)
    function reserveFormat(){
      for(i=0;i<=8;i++){ if(i!==6){ set(8,i,false); set(i,8,false); } }
      for(i=0;i<8;i++){ set(size-1-i,8,false); set(8,size-1-i,false); }
      set(8,size-8,true); // dark module
    }
    reserveFormat();
    if(ver>=7){
      for(i=0;i<18;i++){ var a=size-11+i%3, bb2=Math.floor(i/3); set(a,bb2,false); set(bb2,a,false); }
    }

    // ---- place data (zigzag) ----
    var bit=0, nbits=out.length*8;
    for(var right=size-1;right>=1;right-=2){
      if(right===6) right=5;
      for(var vert=0;vert<size;vert++){
        for(j=0;j<2;j++){
          var x=right-j, upward=((right+1)&2)===0, y=upward?size-1-vert:vert;
          if(!fn[y][x] && bit<nbits){ m[y][x]=getBit(out[bit>>>3],7-(bit&7))?1:0; bit++; }
        }
      }
    }

    // ---- masking helpers ----
    function maskFn(mask,x,y){
      switch(mask){
        case 0:return (x+y)%2===0;
        case 1:return y%2===0;
        case 2:return x%3===0;
        case 3:return (x+y)%3===0;
        case 4:return (Math.floor(x/3)+Math.floor(y/2))%2===0;
        case 5:return (x*y)%2+(x*y)%3===0;
        case 6:return ((x*y)%2+(x*y)%3)%2===0;
        default:return ((x+y)%2+(x*y)%3)%2===0;
      }
    }
    function applyMask(mask){ for(var y=0;y<size;y++)for(var x=0;x<size;x++) if(!fn[y][x]&&maskFn(mask,x,y)) m[y][x]^=1; }

    function drawFormat(mask){
      var d=(0<<3)|mask; // EC level M format bits = 00
      var rem=d; for(i=0;i<10;i++) rem=(rem<<1)^((rem>>>9)*0x537);
      var bits=((d<<10)|rem)^0x5412;
      for(i=0;i<=5;i++) m[i][8]=getBit(bits,i)?1:0;
      m[7][8]=getBit(bits,6)?1:0; m[8][8]=getBit(bits,7)?1:0; m[8][7]=getBit(bits,8)?1:0;
      for(i=9;i<15;i++) m[8][14-i]=getBit(bits,i)?1:0;
      for(i=0;i<8;i++) m[8][size-1-i]=getBit(bits,i)?1:0;
      for(i=8;i<15;i++) m[size-15+i][8]=getBit(bits,i)?1:0;
      m[size-8][8]=1;
    }
    function drawVersion(){
      if(ver<7) return;
      var rem=ver; for(i=0;i<12;i++) rem=(rem<<1)^((rem>>>11)*0x1F25);
      var bits=(ver<<12)|rem;
      for(i=0;i<18;i++){ var b=getBit(bits,i)?1:0, a=size-11+i%3, c=Math.floor(i/3); m[a][c]=b; m[c][a]=b; }
    }

    // ---- penalty scoring to pick the best mask ----
    function penalty(){
      var p=0,dark=0,x,y,run;
      for(y=0;y<size;y++){ run=1; for(x=1;x<size;x++){ if(m[y][x]===m[y][x-1]){run++; if(run===5)p+=3; else if(run>5)p++;} else run=1; } }
      for(x=0;x<size;x++){ run=1; for(y=1;y<size;y++){ if(m[y][x]===m[y-1][x]){run++; if(run===5)p+=3; else if(run>5)p++;} else run=1; } }
      for(y=0;y<size-1;y++)for(x=0;x<size-1;x++){ var c=m[y][x]; if(c===m[y][x+1]&&c===m[y+1][x]&&c===m[y+1][x+1])p+=3; }
      var A=[1,0,1,1,1,0,1,0,0,0,0], B=[0,0,0,0,1,0,1,1,1,0,1];
      function match(get,len,i,pat){ for(var q=0;q<11;q++) if(get(i+q)!==pat[q]) return false; return true; }
      for(y=0;y<size;y++)for(x=0;x<=size-11;x++){ var g=function(k){return m[y][k];}; if(match(g,0,x,A)||match(g,0,x,B))p+=40; }
      for(x=0;x<size;x++)for(y=0;y<=size-11;y++){ var g2=function(k){return m[k][x];}; if(match(g2,0,y,A)||match(g2,0,y,B))p+=40; }
      for(y=0;y<size;y++)for(x=0;x<size;x++) if(m[y][x])dark++;
      p+=Math.floor(Math.abs(dark*100/(size*size)-50)/5)*10;
      return p;
    }

    // Try all 8 masks, keep the lowest penalty.
    var best=-1,bestP=1e9,snap=null;
    for(var mask=0;mask<8;mask++){
      applyMask(mask); drawFormat(mask); drawVersion();
      var pp=penalty();
      if(pp<bestP){ bestP=pp; best=mask; }
      applyMask(mask); // undo (XOR again); format cells get overwritten next round
    }
    applyMask(best); drawFormat(best); drawVersion();

    // return as boolean matrix
    var res=[]; for(y=0;y<size;y++){ var r=[]; for(x=0;x<size;x++) r.push(m[y][x]===1); res.push(r); }
    return res;
  }

  // Render into a container element. Returns true on success, false if payload
  // too large (caller should fall back to text). White quiet-zone always kept.
  function render(container, text, px){
    container.innerHTML='';
    var mods=build(text);
    if(!mods){ return false; }
    var n=mods.length, border=4, tot=n+border*2;
    var scale=Math.max(2,Math.floor((px||220)/tot));
    var cv=document.createElement('canvas');
    cv.width=cv.height=tot*scale;
    var g=cv.getContext('2d');
    g.fillStyle='#fff'; g.fillRect(0,0,cv.width,cv.height);
    g.fillStyle='#000';
    for(var y=0;y<n;y++)for(var x=0;x<n;x++) if(mods[y][x]) g.fillRect((x+border)*scale,(y+border)*scale,scale,scale);
    var box=document.createElement('div'); box.className='qr-box'; box.appendChild(cv);
    container.appendChild(box);
    return true;
  }
  return { render:render };
})();

/* ===========================================================================
 * 2) i18n  (EN / FA)
 * ==========================================================================*/
var I18N = {
  en:{
    connecting:"Connecting to device…", lock:"Lock", locked:"Locked",
    retry:"Retry", conn_fail:"Cannot reach the device", conn_fail_hint:"Make sure you are connected to the DibaVault Wi-Fi.",
    // wizard
    setup_title:"Device setup", setup_sub:"Let's provision your DibaVault.",
    step_hw:"Hardware", step_wallet:"Wallet", step_wifi:"Wi-Fi & finish",
    hw_title:"Hardware configuration", hw_sub:"Configure the display and buttons for on-device confirmation.",
    display_type:"Display type", none:"None", confirm_mode:"Confirmation mode",
    cm_none:"None (no physical confirm)", cm_1:"1 button", cm_2:"2 buttons", cm_3:"3 buttons",
    i2c_sda:"I²C SDA GPIO", i2c_scl:"I²C SCL GPIO", i2c_addr:"I²C address (hex)",
    btn_ok:"OK button GPIO", btn_cancel:"Cancel button GPIO", btn_up:"Up button GPIO", btn_down:"Down button GPIO",
    active_low:"Buttons are active-low", hw_warn:"You selected no display and no buttons. Transactions cannot be verified on a trusted screen, which significantly weakens security. Use only for watch-only / testing.",
    save_continue:"Save & continue",
    // wallet
    wallet_title:"Create or import wallet", create_new:"Create new", import_existing:"Import existing",
    word_count:"Recovery phrase length", w12:"12 words", w18:"18 words", w24:"24 words",
    pin:"PIN", pin_hint:"6–12 digits", passphrase:"Passphrase (optional)", passphrase_hint:"Extra 25th-word secret. Leave empty if unsure.",
    generate:"Generate wallet", import_phrase:"Recovery phrase", import_hint:"Enter your 12/18/24 words separated by spaces.",
    import_btn:"Import wallet",
    mnemonic_title:"Your recovery phrase", mnemonic_warn_1:"Write these words down offline, in order.",
    mnemonic_warn_2:"They are shown ONCE. dibachain can never recover them. Anyone with these words controls your funds.",
    written_down:"I have written them down and stored them securely.", continue:"Continue",
    // wifi
    wifi_title:"Wi-Fi", wifi_sub:"Optional. You can run fully offline (air-gapped) too.",
    ap_ssid:"Access-point name (AP SSID)", ap_pass:"Access-point password",
    sta_ssid:"Home Wi-Fi name (STA SSID)", sta_pass:"Home Wi-Fi password",
    sta_updates_only:"Only use home Wi-Fi for updates (recommended)", finish:"Finish setup",
    // unlock
    unlock_title:"Enter PIN", unlock_sub:"Unlock your DibaVault to continue.",
    unlock_btn:"Unlock", attempts_left:"attempts left", wrong_pin:"Wrong PIN", clear:"Clear",
    // dashboard tabs
    tab_networks:"Networks", tab_accounts:"Accounts", tab_send:"Send", tab_offline:"Offline signing", tab_settings:"Settings",
    // networks
    networks_title:"Networks & RPCs", networks_sub:"Add any chain and your own RPC endpoint.",
    add_network:"Add network", family:"Family", name:"Name", chain_id:"EVM chain ID",
    rpc_url:"RPC URL", symbol:"Symbol", decimals:"Decimals", explorer:"Explorer URL (optional)",
    add:"Add", delete:"Delete", no_networks:"No networks yet. Add your first chain above.",
    // accounts
    accounts_title:"Accounts & balances", select_network:"Select network",
    account:"Account", balance:"Balance", refresh:"Refresh", copy:"Copy", copied:"Copied",
    watch_only:"Read-only — safe to use in watch-only / hotspot mode.", scan_qr:"Scan to receive",
    // send
    send_title:"Send transaction", to_addr:"Recipient address", amount:"Amount",
    token_contract:"Token contract (optional)", token_decimals:"Token decimals",
    build_tx:"Review transaction", review_title:"Review transaction", sign_device:"Sign on device",
    fee:"Network fee", nonce:"Nonce", gas:"Gas", broadcast:"Broadcast", signed_tx:"Signed transaction",
    txid:"Transaction ID", edit:"Edit", confirm_device:"Confirm on your device",
    confirm_device_sub:"Check the details on the DibaVault screen and approve.",
    // offline
    offline_title:"Air-gapped offline signing", offline_toggle:"Air-gapped mode",
    offline_on:"Radio is OFF. The device is not connected to any network.",
    offline_off:"Turn on to disable all radios and sign fully offline.",
    offline_sub:"Fill the transaction below (or paste unsigned JSON). The device signs it without ever touching the network. Carry the signed result to an online device via QR.",
    unsigned_json:"Unsigned transaction JSON (optional)", sign_offline:"Sign offline",
    signed_result:"Signed result", carry_qr:"Scan on your online device to broadcast",
    paste_signed:"Paste a signed result to decode/verify",
    // settings
    settings_title:"Settings", wifi_settings:"Wi-Fi", save:"Save",
    ota_title:"Firmware update", ota_check:"Check for updates", ota_current:"Current version",
    ota_latest:"Latest version", ota_apply:"Install update", ota_uptodate:"You are on the latest version.",
    ota_available:"Update available", hw_info:"Hardware info", lock_now:"Lock device now",
    updating:"Updating firmware… do not power off.",
    // generic
    required:"required", loading:"Loading…", saved:"Saved", error:"Error", close:"Close", warning:"Warning"
  },
  fa:{
    connecting:"در حال اتصال به دستگاه…", lock:"قفل", locked:"قفل شده",
    retry:"تلاش دوباره", conn_fail:"دستگاه در دسترس نیست", conn_fail_hint:"مطمئن شوید به وای‌فای DibaVault متصل هستید.",
    setup_title:"راه‌اندازی دستگاه", setup_sub:"بیایید DibaVault شما را آماده کنیم.",
    step_hw:"سخت‌افزار", step_wallet:"کیف پول", step_wifi:"وای‌فای و پایان",
    hw_title:"پیکربندی سخت‌افزار", hw_sub:"نمایشگر و دکمه‌ها را برای تأیید روی دستگاه تنظیم کنید.",
    display_type:"نوع نمایشگر", none:"هیچ‌کدام", confirm_mode:"حالت تأیید",
    cm_none:"بدون تأیید فیزیکی", cm_1:"۱ دکمه", cm_2:"۲ دکمه", cm_3:"۳ دکمه",
    i2c_sda:"پایه SDA (I²C)", i2c_scl:"پایه SCL (I²C)", i2c_addr:"آدرس I²C (هگز)",
    btn_ok:"پایه دکمه تأیید", btn_cancel:"پایه دکمه لغو", btn_up:"پایه دکمه بالا", btn_down:"پایه دکمه پایین",
    active_low:"دکمه‌ها active-low هستند", hw_warn:"شما نمایشگر و دکمه انتخاب نکردید. تراکنش‌ها روی صفحهٔ مطمئن قابل بررسی نیستند و امنیت به‌شدت کاهش می‌یابد. فقط برای حالت فقط-مشاهده / آزمایش استفاده شود.",
    save_continue:"ذخیره و ادامه",
    wallet_title:"ساخت یا وارد کردن کیف پول", create_new:"ساخت جدید", import_existing:"وارد کردن",
    word_count:"طول عبارت بازیابی", w12:"۱۲ کلمه", w18:"۱۸ کلمه", w24:"۲۴ کلمه",
    pin:"پین", pin_hint:"۶ تا ۱۲ رقم", passphrase:"عبارت عبور (اختیاری)", passphrase_hint:"کلمهٔ بیست‌وپنجم اضافی. اگر مطمئن نیستید خالی بگذارید.",
    generate:"ساخت کیف پول", import_phrase:"عبارت بازیابی", import_hint:"۱۲/۱۸/۲۴ کلمه را با فاصله وارد کنید.",
    import_btn:"وارد کردن کیف پول",
    mnemonic_title:"عبارت بازیابی شما", mnemonic_warn_1:"این کلمات را به‌ترتیب و آفلاین یادداشت کنید.",
    mnemonic_warn_2:"این کلمات فقط یک‌بار نمایش داده می‌شوند. dibachain هرگز نمی‌تواند آن‌ها را بازیابی کند. هرکس این کلمات را داشته باشد به دارایی شما دسترسی دارد.",
    written_down:"آن‌ها را یادداشت و ایمن نگه‌داری کردم.", continue:"ادامه",
    wifi_title:"وای‌فای", wifi_sub:"اختیاری. می‌توانید کاملاً آفلاین (ایزوله) هم کار کنید.",
    ap_ssid:"نام اکسس‌پوینت (AP SSID)", ap_pass:"رمز اکسس‌پوینت",
    sta_ssid:"نام وای‌فای خانه (STA SSID)", sta_pass:"رمز وای‌فای خانه",
    sta_updates_only:"وای‌فای خانه فقط برای به‌روزرسانی (توصیه‌شده)", finish:"پایان راه‌اندازی",
    unlock_title:"پین را وارد کنید", unlock_sub:"برای ادامه DibaVault را باز کنید.",
    unlock_btn:"باز کردن", attempts_left:"تلاش باقی‌مانده", wrong_pin:"پین اشتباه", clear:"پاک کردن",
    tab_networks:"شبکه‌ها", tab_accounts:"حساب‌ها", tab_send:"ارسال", tab_offline:"امضای آفلاین", tab_settings:"تنظیمات",
    networks_title:"شبکه‌ها و RPCها", networks_sub:"هر زنجیره و RPC دلخواه خود را اضافه کنید.",
    add_network:"افزودن شبکه", family:"خانواده", name:"نام", chain_id:"شناسه زنجیره EVM",
    rpc_url:"آدرس RPC", symbol:"نماد", decimals:"اعشار", explorer:"آدرس اکسپلورر (اختیاری)",
    add:"افزودن", delete:"حذف", no_networks:"هنوز شبکه‌ای نیست. اولین زنجیره را بالا اضافه کنید.",
    accounts_title:"حساب‌ها و موجودی", select_network:"انتخاب شبکه",
    account:"حساب", balance:"موجودی", refresh:"تازه‌سازی", copy:"کپی", copied:"کپی شد",
    watch_only:"فقط-خواندنی — استفاده در حالت فقط-مشاهده / هات‌اسپات ایمن است.", scan_qr:"برای دریافت اسکن کنید",
    send_title:"ارسال تراکنش", to_addr:"آدرس گیرنده", amount:"مقدار",
    token_contract:"قرارداد توکن (اختیاری)", token_decimals:"اعشار توکن",
    build_tx:"بررسی تراکنش", review_title:"بررسی تراکنش", sign_device:"امضا روی دستگاه",
    fee:"کارمزد شبکه", nonce:"نانس", gas:"گس", broadcast:"ارسال به شبکه", signed_tx:"تراکنش امضاشده",
    txid:"شناسه تراکنش", edit:"ویرایش", confirm_device:"روی دستگاه تأیید کنید",
    confirm_device_sub:"جزئیات را روی صفحهٔ DibaVault بررسی و تأیید کنید.",
    offline_title:"امضای آفلاین ایزوله", offline_toggle:"حالت ایزوله (Air-gapped)",
    offline_on:"رادیو خاموش است. دستگاه به هیچ شبکه‌ای متصل نیست.",
    offline_off:"برای خاموش کردن همهٔ رادیوها و امضای کاملاً آفلاین روشن کنید.",
    offline_sub:"تراکنش زیر را پر کنید (یا JSON امضانشده را بچسبانید). دستگاه بدون اتصال به شبکه آن را امضا می‌کند. نتیجهٔ امضاشده را با QR به دستگاه آنلاین ببرید.",
    unsigned_json:"JSON تراکنش امضانشده (اختیاری)", sign_offline:"امضای آفلاین",
    signed_result:"نتیجهٔ امضاشده", carry_qr:"روی دستگاه آنلاین اسکن کنید تا ارسال شود",
    paste_signed:"یک نتیجهٔ امضاشده برای رمزگشایی بچسبانید",
    settings_title:"تنظیمات", wifi_settings:"وای‌فای", save:"ذخیره",
    ota_title:"به‌روزرسانی firmware", ota_check:"بررسی به‌روزرسانی", ota_current:"نسخهٔ فعلی",
    ota_latest:"آخرین نسخه", ota_apply:"نصب به‌روزرسانی", ota_uptodate:"شما آخرین نسخه را دارید.",
    ota_available:"به‌روزرسانی موجود است", hw_info:"اطلاعات سخت‌افزار", lock_now:"قفل کردن دستگاه",
    updating:"در حال به‌روزرسانی… دستگاه را خاموش نکنید.",
    required:"الزامی", loading:"در حال بارگذاری…", saved:"ذخیره شد", error:"خطا", close:"بستن", warning:"هشدار"
  }
};

/* ===========================================================================
 * 3) Small helpers
 * ==========================================================================*/
var S = { lang:"en", status:null, view:null, tab:"networks", networks:[], airgap:false, review:null };

function t(k){ var d=I18N[S.lang]; return (d && d[k]!=null) ? d[k] : (I18N.en[k]!=null?I18N.en[k]:k); }
function $(sel,root){ return (root||document).querySelector(sel); }
function el(tag,attrs,kids){
  var e=document.createElement(tag);
  if(attrs) for(var k in attrs){
    if(k==="class")e.className=attrs[k];
    else if(k==="html")e.innerHTML=attrs[k];
    else if(k==="text")e.textContent=attrs[k];
    else if(k.slice(0,2)==="on")e.addEventListener(k.slice(2).toLowerCase(),attrs[k]);
    else if(attrs[k]===true)e.setAttribute(k,"");
    else if(attrs[k]!=null&&attrs[k]!==false)e.setAttribute(k,attrs[k]);
  }
  if(kids!=null){ (Array.isArray(kids)?kids:[kids]).forEach(function(c){
    if(c==null)return; e.appendChild(typeof c==="string"?document.createTextNode(c):c);
  }); }
  return e;
}
function mount(node){ var a=$("#app"); a.innerHTML=""; a.appendChild(node); }

function toast(msg,kind,code){
  var host=$("#toasts");
  var node=el("div",{class:"toast "+(kind||"")},[ el("span",{text:msg}), code?el("span",{class:"code",text:code}):null ]);
  host.appendChild(node);
  setTimeout(function(){ node.style.opacity="0"; setTimeout(function(){ node.remove(); },250); }, kind==="err"?6000:3000);
}
function copyText(txt){
  if(navigator.clipboard&&navigator.clipboard.writeText){ navigator.clipboard.writeText(txt).then(function(){ toast(t("copied"),"ok"); }); }
  else{ var ta=el("textarea"); ta.value=txt; document.body.appendChild(ta); ta.select();
        try{ document.execCommand("copy"); toast(t("copied"),"ok"); }catch(e){} ta.remove(); }
}

// JSON fetch wrapper. Throws {code,message} on non-2xx. X-DV-Origin added by host.
async function api(path,method,body){
  var opts={ method:method||"GET", headers:{} };
  if(body!=null){ opts.headers["Content-Type"]="application/json"; opts.body=JSON.stringify(body); }
  var r=await fetch(path,opts);
  var txt=await r.text(), j={};
  if(txt){ try{ j=JSON.parse(txt); }catch(e){ j={message:txt}; } }
  if(!r.ok) throw { code:j.error||("HTTP_"+r.status), message:j.message||r.statusText };
  return j;
}
// Show an API error as a toast.
function apiErr(e){ toast(e&&e.message?e.message:t("error"),"err", e&&e.code?e.code:null); }

// Field builder: label + input/select/textarea
function field(labelKey,input,hintKey,required){
  var lab=el("label",{},[ t(labelKey), required?el("span",{class:"req",text:" *"}):null ]);
  var wrap=el("div",{class:"field"},[lab,input]);
  if(hintKey) wrap.appendChild(el("small",{text:t(hintKey)}));
  return wrap;
}
function inp(name,attrs){ return el("input",Object.assign({name:name,autocomplete:"off"},attrs||{})); }
function sel(name,options,value){
  var s=el("select",{name:name});
  options.forEach(function(o){ var op=el("option",{value:o.v,text:o.l}); if(o.v===value)op.selected=true; s.appendChild(op); });
  return s;
}
function formValues(form){
  var o={};
  form.querySelectorAll("input,select,textarea").forEach(function(f){
    if(!f.name)return;
    o[f.name]= f.type==="checkbox"?f.checked : f.value;
  });
  return o;
}

/* ===========================================================================
 * 4) Modal (device confirmation etc.)
 * ==========================================================================*/
function showModal(content){
  var host=$("#modalHost"); host.innerHTML=""; host.hidden=false;
  host.appendChild(el("div",{class:"modal"},content));
}
function closeModal(){ var host=$("#modalHost"); host.hidden=true; host.innerHTML=""; }
function deviceWaitModal(){
  showModal([
    el("div",{class:"device-anim",html:'<svg width="30" height="30" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><rect x="5" y="2" width="14" height="20" rx="2"/><line x1="9" y1="18" x2="15" y2="18"/></svg>'}),
    el("h3",{text:t("confirm_device")}),
    el("p",{class:"center",text:t("confirm_device_sub")}),
    el("div",{class:"spinner mt"})
  ]);
}

/* ===========================================================================
 * 5) Top bar / chrome
 * ==========================================================================*/
function applyLang(){
  document.documentElement.lang=S.lang;
  document.documentElement.dir = S.lang==="fa"?"rtl":"ltr";
  $("#langToggle").textContent = S.lang==="fa"?"EN":"FA";
  document.querySelectorAll("[data-i18n]").forEach(function(n){ n.textContent=t(n.getAttribute("data-i18n")); });
  render(); // re-render current view in new language
}
function updateChrome(){
  var st=S.status||{};
  $("#topbar").hidden=false; $("#footer").hidden=false;
  var badge=$("#modeBadge"); var mode="--", cls="badge badge-mode";
  if(S.airgap){ mode="AIR-GAPPED"; cls+=" air"; }
  else if(st.mode==="sta"||st.sta_connected){ mode="STA"; cls+=" sta"; }
  else{ mode="AP"; cls+=" ap"; }
  badge.textContent=mode; badge.className=cls;
  $("#lockBtn").hidden = !(st.provisioned && !st.locked);
  $("#fwline").textContent = st.fw_version?("fw "+st.fw_version):"";
}

/* ===========================================================================
 * 6) Views
 * ==========================================================================*/

// ---- connection failure ----
function viewConnFail(){
  mount(el("div",{class:"view"},[
    el("div",{class:"card center"},[
      el("h2",{text:t("conn_fail")}),
      el("p",{text:t("conn_fail_hint")}),
      el("button",{class:"btn btn-primary",onClick:boot},t("retry"))
    ])
  ]));
}

/* ---------------- A) SETUP WIZARD ---------------- */
var wiz = { step:1, hw:null, walletMode:"new", provisionId:null };

function stepper(cur){
  var steps=[["1","step_hw"],["2","step_wallet"],["3","step_wifi"]];
  return el("div",{class:"stepper"}, steps.map(function(s,i){
    var idx=i+1, cls="step"+(idx===cur?" active":idx<cur?" done":"");
    return el("div",{class:cls},[ el("span",{class:"n",text:s[0]}), t(s[1]) ]);
  }));
}

function viewSetup(){
  updateChrome();
  var body;
  if(wiz.step===1) body=setupHardware();
  else if(wiz.step===2) body=setupWallet();
  else body=setupWifi();
  mount(el("div",{class:"view"},[
    el("div",{class:"card"},[
      el("h1",{text:t("setup_title")}),
      el("p",{text:t("setup_sub")}),
      stepper(wiz.step)
    ]),
    body
  ]));
}

function setupHardware(){
  var form=el("form",{class:"card"});
  var displaySel=sel("display",[{v:"none",l:t("none")},{v:"ssd1306",l:"SSD1306"},{v:"sh1106",l:"SH1106"},{v:"st7789",l:"ST7789"}],"ssd1306");
  var confirmSel=sel("confirm",[{v:"none",l:t("cm_none")},{v:"1btn",l:t("cm_1")},{v:"2btn",l:t("cm_2")},{v:"3btn",l:t("cm_3")}],"2btn");
  var warn=el("div",{class:"alert alert-warn hidden",text:t("hw_warn")});
  function checkWarn(){ warn.classList.toggle("hidden", !(displaySel.value==="none"&&confirmSel.value==="none")); }
  displaySel.addEventListener("change",checkWarn); confirmSel.addEventListener("change",checkWarn);

  form.append(
    el("h2",{text:t("hw_title")}),
    el("p",{text:t("hw_sub")}),
    el("div",{class:"grid grid-2"},[
      field("display_type",displaySel),
      field("confirm_mode",confirmSel)
    ]),
    el("h3",{class:"mt",text:"I²C"}),
    el("div",{class:"grid grid-2"},[
      field("i2c_sda",inp("sda",{type:"number",value:"21"})),
      field("i2c_scl",inp("scl",{type:"number",value:"22"})),
      field("i2c_addr",inp("addr",{value:"0x3C"}))
    ]),
    el("h3",{class:"mt",text:t("confirm_mode")}),
    el("div",{class:"grid grid-2"},[
      field("btn_ok",inp("btn_ok",{type:"number",value:"25"})),
      field("btn_cancel",inp("btn_cancel",{type:"number",value:"26"})),
      field("btn_up",inp("btn_up",{type:"number",value:"32"})),
      field("btn_down",inp("btn_down",{type:"number",value:"33"}))
    ]),
    el("div",{class:"checkbox-row"},[ inp("active_low",{type:"checkbox",checked:true}), el("label",{text:t("active_low")}) ]),
    warn,
    el("button",{class:"btn btn-primary btn-block mt",type:"submit"},t("save_continue"))
  );
  checkWarn();
  form.addEventListener("submit",async function(ev){
    ev.preventDefault();
    var v=formValues(form);
    var payload={
      display:v.display, i2c:{sda:+v.sda,scl:+v.scl,addr:v.addr},
      confirm:v.confirm, buttons:{ok:+v.btn_ok,cancel:+v.btn_cancel,up:+v.btn_up,down:+v.btn_down},
      active_low:v.active_low
    };
    var btn=form.querySelector("button[type=submit]"); btn.disabled=true;
    try{ await api("/api/hal","POST",payload); wiz.hw=payload; wiz.step=2; render(); }
    catch(e){ apiErr(e); btn.disabled=false; }
  });
  return form;
}

function setupWallet(){
  var wrap=el("div");
  var tabs=el("div",{class:"row mb"},[
    el("button",{class:"btn "+(wiz.walletMode==="new"?"btn-primary":"btn-ghost"),onClick:function(){wiz.walletMode="new";render();}},t("create_new")),
    el("button",{class:"btn "+(wiz.walletMode==="import"?"btn-primary":"btn-ghost"),onClick:function(){wiz.walletMode="import";render();}},t("import_existing"))
  ]);
  wrap.appendChild(el("div",{class:"card"},[ el("h2",{text:t("wallet_title")}), tabs,
    wiz.walletMode==="new"?walletNew():walletImport() ]));
  return wrap;
}

function walletNew(){
  var form=el("form");
  form.append(
    field("word_count",sel("words",[{v:"12",l:t("w12")},{v:"18",l:t("w18")},{v:"24",l:t("w24")}],"24")),
    field("pin",inp("pin",{type:"password",inputmode:"numeric",pattern:"[0-9]*"}),"pin_hint",true),
    field("passphrase",inp("passphrase",{type:"password"}),"passphrase_hint"),
    el("button",{class:"btn btn-primary btn-block mt",type:"submit"},t("generate"))
  );
  form.addEventListener("submit",async function(ev){
    ev.preventDefault();
    var v=formValues(form);
    if(!/^[0-9]{6,12}$/.test(v.pin)){ toast(t("pin")+": "+t("pin_hint"),"err"); return; }
    var btn=form.querySelector("button"); btn.disabled=true;
    try{
      var res=await api("/api/provision/new","POST",{words:+v.words,pin:v.pin,passphrase:v.passphrase||""});
      wiz.provisionId=res.id||res.provision_id||null;
      showMnemonic(res.mnemonic||"");
    }catch(e){ apiErr(e); btn.disabled=false; }
  });
  return form;
}

function walletImport(){
  var form=el("form");
  form.append(
    field("import_phrase",el("textarea",{name:"mnemonic",placeholder:"word1 word2 …"}),"import_hint",true),
    field("pin",inp("pin",{type:"password",inputmode:"numeric",pattern:"[0-9]*"}),"pin_hint",true),
    field("passphrase",inp("passphrase",{type:"password"}),"passphrase_hint"),
    el("button",{class:"btn btn-primary btn-block mt",type:"submit"},t("import_btn"))
  );
  form.addEventListener("submit",async function(ev){
    ev.preventDefault();
    var v=formValues(form);
    if(!/^[0-9]{6,12}$/.test(v.pin)){ toast(t("pin")+": "+t("pin_hint"),"err"); return; }
    var btn=form.querySelector("button"); btn.disabled=true;
    try{
      await api("/api/provision/import","POST",{mnemonic:(v.mnemonic||"").trim().replace(/\s+/g," "),pin:v.pin,passphrase:v.passphrase||""});
      wiz.step=3; render();
    }catch(e){ apiErr(e); btn.disabled=false; }
  });
  return form;
}

// The critical one-time mnemonic reveal.
function showMnemonic(mnemonic){
  var words=(mnemonic||"").trim().split(/\s+/).filter(Boolean);
  var grid=el("div",{class:"mnemonic"}, words.map(function(w,i){
    return el("div",{class:"mword"},[ el("span",{class:"idx",text:(i+1)+"."}), w ]);
  }));
  var chk=inp("ack",{type:"checkbox"});
  var cont=el("button",{class:"btn btn-primary btn-block mt",disabled:true},t("continue"));
  chk.addEventListener("change",function(){ cont.disabled=!chk.checked; });
  cont.addEventListener("click",async function(){
    cont.disabled=true;
    try{ await api("/api/provision/confirm","POST",{id:wiz.provisionId}); wiz.step=3; render(); }
    catch(e){ apiErr(e); cont.disabled=false; }
  });
  mount(el("div",{class:"view"},[
    el("div",{class:"card card-accent"},[
      el("h2",{text:t("mnemonic_title")}),
      el("div",{class:"alert alert-danger"},[
        el("strong",{text:"⚠ "+t("mnemonic_warn_1")+" "}),
        el("div",{class:"mt",text:t("mnemonic_warn_2")})
      ]),
      grid,
      el("div",{class:"checkbox-row"},[ chk, el("label",{text:t("written_down")}) ]),
      cont
    ])
  ]));
}

function setupWifi(){
  var form=el("form",{class:"card"});
  form.append(
    el("h2",{text:t("wifi_title")}),
    el("p",{text:t("wifi_sub")}),
    el("div",{class:"grid grid-2"},[
      field("ap_ssid",inp("ap_ssid",{value:"DibaVault"})),
      field("ap_pass",inp("ap_pass",{type:"text"}))
    ]),
    el("div",{class:"grid grid-2"},[
      field("sta_ssid",inp("sta_ssid")),
      field("sta_pass",inp("sta_pass",{type:"password"}))
    ]),
    el("div",{class:"checkbox-row"},[ inp("sta_updates_only",{type:"checkbox",checked:true}), el("label",{text:t("sta_updates_only")}) ]),
    el("button",{class:"btn btn-primary btn-block mt",type:"submit"},t("finish"))
  );
  form.addEventListener("submit",async function(ev){
    ev.preventDefault();
    var v=formValues(form);
    var btn=form.querySelector("button"); btn.disabled=true;
    try{
      await api("/api/wifi","POST",v);
      await boot(); // reload status -> should now be provisioned
    }catch(e){ apiErr(e); btn.disabled=false; }
  });
  return form;
}

/* ---------------- B) UNLOCK ---------------- */
function viewUnlock(){
  updateChrome();
  var pin="";
  var disp=el("div",{class:"pin-display"});
  function refresh(){ disp.textContent="•".repeat(pin.length); }
  function press(d){ if(pin.length<12){ pin+=d; refresh(); } }
  var keys=[];
  ["1","2","3","4","5","6","7","8","9"].forEach(function(d){ keys.push(el("button",{class:"pin-key",onClick:function(){press(d);}},d)); });
  keys.push(el("button",{class:"pin-key",onClick:function(){ pin=""; refresh(); },title:t("clear")},"⌫"));
  keys.push(el("button",{class:"pin-key",onClick:function(){press("0");}},"0"));
  var submit=el("button",{class:"pin-key accent-text",title:t("unlock_btn")},"→");

  submit.addEventListener("click",async function(){
    if(pin.length<6){ toast(t("pin")+": "+t("pin_hint"),"err"); return; }
    submit.disabled=true;
    try{
      var res=await api("/api/unlock","POST",{pin:pin});
      if(res.ok){ await boot(); }
      else{ toast(t("wrong_pin")+(res.attempts_left!=null?(" — "+res.attempts_left+" "+t("attempts_left")):""),"err"); pin=""; refresh(); submit.disabled=false; }
    }catch(e){
      var msg=t("wrong_pin"); if(e.message)msg=e.message;
      toast(msg,"err",e.code); pin=""; refresh(); submit.disabled=false;
    }
  });
  keys.push(submit);

  mount(el("div",{class:"view"},[
    el("div",{class:"card center"},[
      el("h1",{text:t("unlock_title")}),
      el("p",{text:t("unlock_sub")}),
      disp,
      el("div",{class:"pin-pad"},keys)
    ])
  ]));
}

/* ---------------- C) DASHBOARD ---------------- */
function viewDashboard(){
  updateChrome();
  var tabs=[["networks","tab_networks"],["accounts","tab_accounts"],["send","tab_send"],["offline","tab_offline"],["settings","tab_settings"]];
  var tabBar=el("div",{class:"tabs"}, tabs.map(function(tb){
    return el("button",{class:"tab"+(S.tab===tb[0]?" active":""),onClick:function(){ S.tab=tb[0]; render(); }},t(tb[1]));
  }));
  var content=el("div");
  mount(el("div",{class:"view"},[ tabBar, content ]));
  if(S.tab==="networks") tabNetworks(content);
  else if(S.tab==="accounts") tabAccounts(content);
  else if(S.tab==="send") tabSend(content);
  else if(S.tab==="offline") tabOffline(content);
  else tabSettings(content);
}

// --- Networks manager ---
async function loadNetworks(){ try{ var r=await api("/api/networks"); S.networks=r.networks||r||[]; }catch(e){ apiErr(e); S.networks=[]; } }

function tabNetworks(root){
  root.innerHTML="";
  var form=el("form",{class:"card card-accent"});
  var famSel=sel("family",[{v:"evm",l:"EVM"},{v:"bitcoin",l:"Bitcoin"},{v:"tron",l:"Tron"},{v:"solana",l:"Solana"}],"evm");
  var chainField=field("chain_id",inp("evm_chain_id",{type:"number",placeholder:"1"}));
  function toggleChain(){ chainField.style.display= famSel.value==="evm"?"":"none"; }
  famSel.addEventListener("change",toggleChain);
  form.append(
    el("h2",{text:t("networks_title")}),
    el("p",{text:t("networks_sub")}),
    el("div",{class:"grid grid-2"},[ field("family",famSel), field("name",inp("name",{placeholder:"My Chain",required:true})) ]),
    chainField,
    field("rpc_url",inp("rpc_url",{placeholder:"https://…",required:true})),
    el("div",{class:"grid grid-2"},[ field("symbol",inp("symbol",{placeholder:"ETH"})), field("decimals",inp("decimals",{type:"number",value:"18"})) ]),
    field("explorer",inp("explorer",{placeholder:"https://…"})),
    el("button",{class:"btn btn-primary btn-block mt",type:"submit"},t("add_network"))
  );
  toggleChain();
  form.addEventListener("submit",async function(ev){
    ev.preventDefault();
    var v=formValues(form);
    var payload={ family:v.family, name:v.name, rpc_url:v.rpc_url, symbol:v.symbol, decimals:+v.decimals||18, explorer:v.explorer||"" };
    if(v.family==="evm") payload.evm_chain_id=+v.evm_chain_id||0;
    var btn=form.querySelector("button"); btn.disabled=true;
    try{ await api("/api/networks","POST",payload); toast(t("saved"),"ok"); await loadNetworks(); render(); }
    catch(e){ apiErr(e); btn.disabled=false; }
  });

  var listCard=el("div",{class:"card"});
  if(!S.networks.length){ listCard.appendChild(el("p",{class:"center",text:t("no_networks")})); }
  else{
    listCard.appendChild(el("div",{class:"list"}, S.networks.map(function(nw,idx){
      var meta=[];
      if(nw.family)meta.push(nw.family);
      if(nw.evm_chain_id)meta.push("chainId "+nw.evm_chain_id);
      if(nw.rpc_url)meta.push(nw.rpc_url);
      return el("div",{class:"list-item"},[
        el("div",{class:"grow"},[
          el("div",{class:"title"},[ (nw.name||"?"), " ", el("span",{class:"tag",text:nw.symbol||""}) ]),
          el("div",{class:"meta",text:meta.join(" · ")})
        ]),
        el("button",{class:"btn btn-danger btn-sm",onClick:async function(){
          try{ await api("/api/networks/"+idx,"DELETE"); await loadNetworks(); render(); }catch(e){ apiErr(e); }
        }},t("delete"))
      ]);
    })));
  }
  root.append(form,listCard);
}

// --- Accounts & balances ---
function netOptions(){ return S.networks.map(function(n,i){ return {v:String(i),l:(n.name||("net "+i))+(n.symbol?(" ("+n.symbol+")"):"")}; }); }

function tabAccounts(root){
  root.innerHTML="";
  if(!S.networks.length){ root.appendChild(el("div",{class:"card center"},[ el("p",{text:t("no_networks")}) ])); return; }
  var card=el("div",{class:"card"});
  var netSel=sel("net",netOptions(),"0");
  var out=el("div",{class:"mt"});
  card.append(
    el("h2",{text:t("accounts_title")}),
    el("div",{class:"alert alert-info",text:t("watch_only")}),
    field("select_network",netSel),
    el("button",{class:"btn btn-primary mt",onClick:function(){ loadAccount(netSel.value,out); }},t("refresh")),
    out
  );
  root.appendChild(card);
  loadAccount(netSel.value,out);
}

async function loadAccount(netIdx,out){
  out.innerHTML=""; out.appendChild(el("div",{class:"center"},[el("div",{class:"spinner"})]));
  var nw=S.networks[netIdx]||{};
  try{
    var acc=await api("/api/accounts?chain="+encodeURIComponent(nw.family||"")+"&net_idx="+netIdx);
    var list=acc.accounts||acc.addresses||(acc.address?[{address:acc.address}]:[]);
    out.innerHTML="";
    if(!list.length){ out.appendChild(el("p",{text:t("account")+": —"})); return; }
    list.forEach(function(a){
      var addr=a.address||a;
      var qrHost=el("div",{class:"qr-wrap"});
      var ok=QR.render(qrHost,addr,200);
      var balSpan=el("span",{class:"v big",text:"…"});
      // fetch balance
      api("/api/balance?net_idx="+netIdx+"&address="+encodeURIComponent(addr)).then(function(b){
        balSpan.textContent=(b.balance!=null?b.balance:"0")+" "+(b.symbol||nw.symbol||"");
      }).catch(function(){ balSpan.textContent="—"; });

      out.appendChild(el("div",{class:"subtle mb"},[
        el("div",{class:"kv"},[ el("span",{class:"k",text:t("balance")}), balSpan ]),
        el("label",{text:t("account")}),
        el("div",{class:"addr"},addr),
        el("div",{class:"row mt"},[
          el("button",{class:"btn btn-ghost btn-sm",onClick:function(){ copyText(addr); }},t("copy")),
          nw.explorer?el("a",{class:"btn btn-ghost btn-sm",href:nw.explorer.replace(/\/$/,"")+"/address/"+addr,target:"_blank",rel:"noopener"},t("explorer")):null
        ]),
        ok?el("div",{class:"center mt"},[ qrHost, el("small",{class:"mt",text:t("scan_qr")}) ]):null
      ]));
    });
  }catch(e){ out.innerHTML=""; apiErr(e); out.appendChild(el("p",{class:"danger-text",text:e.message||t("error")})); }
}

// --- Send transaction ---
function sendForm(prefix){
  // Shared builder used by Send + Offline tabs. `prefix` distinguishes ids.
  var form=el("form");
  var netSel=sel("net",netOptions(),"0");
  form.append(
    field("select_network",netSel),
    field("to_addr",inp("to",{placeholder:"0x… / bc1… ",required:true}),null,true),
    field("amount",inp("amount",{type:"text",inputmode:"decimal",placeholder:"0.0",required:true}),null,true),
    el("div",{class:"grid grid-2"},[
      field("token_contract",inp("token",{placeholder:"0x… (leave empty for native)"})),
      field("token_decimals",inp("token_decimals",{type:"number",placeholder:"18"}))
    ])
  );
  return { form:form, values:function(){ return formValues(form); } };
}

function tabSend(root){
  root.innerHTML="";
  if(!S.networks.length){ root.appendChild(el("div",{class:"card center"},[ el("p",{text:t("no_networks")}) ])); return; }
  var card=el("div",{class:"card"});
  var sf=sendForm("send");
  var reviewHost=el("div",{class:"mt"});
  var buildBtn=el("button",{class:"btn btn-primary btn-block mt",type:"submit"},t("build_tx"));
  sf.form.appendChild(buildBtn);
  sf.form.addEventListener("submit",async function(ev){
    ev.preventDefault();
    var v=sf.values();
    var payload={ net_idx:+v.net, to:v.to, amount:v.amount, token:v.token||null, token_decimals:v.token_decimals?+v.token_decimals:null };
    buildBtn.disabled=true;
    try{
      var built=await api("/api/tx/build","POST",payload);
      S.review=built; renderReview(reviewHost,built,+v.net);
    }catch(e){ apiErr(e); }
    buildBtn.disabled=false;
  });
  card.append(el("h2",{text:t("send_title")}), sf.form, reviewHost);
  root.appendChild(card);
}

function renderReview(host,tx,netIdx){
  host.innerHTML="";
  function kv(k,val){ return val==null||val===""?null:el("div",{class:"kv"},[ el("span",{class:"k",text:t(k)}), el("span",{class:"v",text:String(val)}) ]); }
  var signBtn=el("button",{class:"btn btn-primary",onClick:function(){ signOnDevice(tx,netIdx,host); }},t("sign_device"));
  host.appendChild(el("div",{class:"card card-accent"},[
    el("h3",{text:t("review_title")}),
    kv("to_addr",tx.to),
    el("div",{class:"kv"},[ el("span",{class:"k",text:t("amount")}), el("span",{class:"v big",text:(tx.amount!=null?tx.amount:"")+" "+(tx.symbol||"")}) ]),
    kv("fee",tx.fee!=null?(tx.fee+" "+(tx.fee_symbol||tx.symbol||"")):null),
    kv("gas",tx.gas||tx.gas_limit),
    kv("nonce",tx.nonce),
    el("div",{class:"row mt"},[ signBtn ])
  ]));
}

async function signOnDevice(tx,netIdx,host){
  deviceWaitModal();
  try{
    var res=await api("/api/tx/sign","POST",{net_idx:netIdx,tx:tx});
    closeModal();
    var out=el("div",{class:"card"});
    out.append(el("h3",{text:t("signed_tx")}));
    if(res.txid) out.appendChild(el("div",{class:"kv"},[ el("span",{class:"k",text:t("txid")}), el("span",{class:"v",text:res.txid}) ]));
    if(res.signed_hex||res.raw){
      var hx=res.signed_hex||res.raw;
      out.appendChild(el("div",{class:"hex-out mt",text:hx}));
      out.appendChild(el("button",{class:"btn btn-ghost btn-sm mt",onClick:function(){ copyText(hx); }},t("copy")));
    }
    out.appendChild(el("button",{class:"btn btn-primary btn-block mt",onClick:async function(){
      try{ var b=await api("/api/tx/broadcast","POST",{net_idx:netIdx,signed_hex:res.signed_hex||res.raw,txid:res.txid});
           toast((b.txid?(t("txid")+": "+b.txid):t("saved")),"ok"); }catch(e){ apiErr(e); }
    }},t("broadcast")));
    host.innerHTML=""; host.appendChild(out);
  }catch(e){ closeModal(); apiErr(e); }
}

// --- Offline / air-gapped signing ---
function tabOffline(root){
  root.innerHTML="";
  var toggle=inp("air",{type:"checkbox"}); toggle.checked=S.airgap;
  var toggleRow=el("div",{class:"toggle-row"+(S.airgap?" on":"")},[
    el("label",{class:"switch"},[ toggle, el("span",{class:"slider"}) ]),
    el("div",{class:"grow"},[
      el("div",{class:"title",text:t("offline_toggle")}),
      el("small",{text:S.airgap?t("offline_on"):t("offline_off")})
    ])
  ]);
  toggle.addEventListener("change",async function(){
    S.airgap=toggle.checked;
    try{ await api("/api/wifi","POST",{airgap:S.airgap}); }catch(e){ /* best-effort; radios also togglable on-device */ }
    render();
  });

  var card=el("div",{class:"card"+(S.airgap?" card-accent":"")});
  card.append(el("h2",{text:t("offline_title")}), toggleRow, el("p",{class:"mt",text:t("offline_sub")}));

  var sf=sendForm("off");
  var jsonField=field("unsigned_json",el("textarea",{name:"json",placeholder:'{"to":"0x…","value":"…"}'}));
  var out=el("div",{class:"mt"});
  var signBtn=el("button",{class:"btn btn-primary btn-block mt",onClick:async function(){
    var v=sf.values();
    var payload;
    var raw=jsonField.querySelector("textarea").value.trim();
    if(raw){ try{ payload={tx:JSON.parse(raw)}; }catch(e){ toast("JSON: "+t("error"),"err"); return; } }
    else payload={ net_idx:+v.net, to:v.to, amount:v.amount, token:v.token||null, token_decimals:v.token_decimals?+v.token_decimals:null };
    signBtn.disabled=true;
    deviceWaitModal();
    try{
      var res=await api("/api/offline/sign","POST",payload);
      closeModal();
      renderSignedResult(out,res.signed_hex||res.raw||"");
    }catch(e){ closeModal(); apiErr(e); }
    signBtn.disabled=false;
  }},t("sign_offline"));

  card.append(sf.form, jsonField, signBtn, out);

  // paste + decode a signed result (verify)
  var pasteTa=el("textarea",{placeholder:"0x…"});
  var pasteCard=el("div",{class:"card"});
  pasteCard.append(el("h3",{text:t("paste_signed")}), pasteTa,
    el("button",{class:"btn btn-ghost mt",onClick:function(){ if(pasteTa.value.trim()) renderSignedResult(out,pasteTa.value.trim()); }},t("signed_result")));

  root.append(card,pasteCard);
}

function renderSignedResult(host,hex){
  host.innerHTML="";
  if(!hex) return;
  var box=el("div",{class:"card card-accent"});
  box.append(el("h3",{text:t("signed_result")}), el("div",{class:"hex-out",text:hex}),
    el("button",{class:"btn btn-ghost btn-sm mt",onClick:function(){ copyText(hex); }},t("copy")));
  var qrHost=el("div",{class:"qr-wrap mt"});
  if(QR.render(qrHost,hex,240)){
    box.append(el("div",{class:"center mt"},[ qrHost, el("small",{class:"mt",text:t("carry_qr")}) ]));
  }else{
    // payload too large for a single QR — ship text only, do NOT show a broken QR.
    box.append(el("div",{class:"alert alert-info mt",text:"QR: "+t("signed_result")+" — payload too large for a single code; use the text above."}));
    // TODO: multi-part / animated QR for very large signed payloads.
  }
  host.appendChild(box);
}

// --- Settings ---
function tabSettings(root){
  root.innerHTML="";

  // Wi-Fi settings
  (async function(){
    var wifiCard=el("div",{class:"card"});
    wifiCard.append(el("h2",{text:t("wifi_settings")}), el("div",{class:"center"},[el("div",{class:"spinner"})]));
    root.appendChild(wifiCard);
    var w={}; try{ w=await api("/api/wifi"); }catch(e){}
    var form=el("form");
    form.append(
      el("div",{class:"grid grid-2"},[ field("ap_ssid",inp("ap_ssid",{value:w.ap_ssid||"DibaVault"})), field("ap_pass",inp("ap_pass",{value:w.ap_pass||""})) ]),
      el("div",{class:"grid grid-2"},[ field("sta_ssid",inp("sta_ssid",{value:w.sta_ssid||""})), field("sta_pass",inp("sta_pass",{type:"password",value:w.sta_pass||""})) ]),
      el("div",{class:"checkbox-row"},[ inp("sta_updates_only",{type:"checkbox",checked:w.sta_updates_only!==false}), el("label",{text:t("sta_updates_only")}) ]),
      el("button",{class:"btn btn-primary mt",type:"submit"},t("save"))
    );
    form.addEventListener("submit",async function(ev){ ev.preventDefault();
      try{ await api("/api/wifi","POST",formValues(form)); toast(t("saved"),"ok"); }catch(e){ apiErr(e); } });
    wifiCard.innerHTML=""; wifiCard.append(el("h2",{text:t("wifi_settings")}),form);
  })();

  // OTA
  var otaCard=el("div",{class:"card"});
  var otaOut=el("div",{class:"mt"});
  otaCard.append(el("h2",{text:t("ota_title")}),
    el("button",{class:"btn btn-primary",onClick:async function(){
      otaOut.innerHTML=""; otaOut.appendChild(el("div",{class:"spinner"}));
      try{
        var c=await api("/api/ota/check");
        otaOut.innerHTML="";
        otaOut.append(
          el("div",{class:"kv"},[ el("span",{class:"k",text:t("ota_current")}), el("span",{class:"v",text:c.current||(S.status&&S.status.fw_version)||"?"}) ]),
          el("div",{class:"kv"},[ el("span",{class:"k",text:t("ota_latest")}), el("span",{class:"v",text:c.latest||"?"}) ])
        );
        if(c.update_available){
          otaOut.append(el("div",{class:"alert alert-info",text:t("ota_available")}),
            el("button",{class:"btn btn-primary",onClick:function(){ applyOta(otaOut); }},t("ota_apply")));
        }else{
          otaOut.append(el("div",{class:"alert alert-info",text:t("ota_uptodate")}));
        }
      }catch(e){ otaOut.innerHTML=""; apiErr(e); }
    }},t("ota_check")),
    otaOut);
  root.appendChild(otaCard);

  // Hardware info + lock
  var hwCard=el("div",{class:"card"});
  var st=S.status||{}; var hw=st.hardware||wiz.hw||{};
  function row(k,v){ return v==null?null:el("div",{class:"kv"},[ el("span",{class:"k",text:k}), el("span",{class:"v",text:String(v)}) ]); }
  hwCard.append(el("h2",{text:t("hw_info")}),
    row("Firmware",st.fw_version),
    row("Chip",st.chip),
    row("Display",hw.display),
    row("Confirm",hw.confirm),
    row("Mode",S.airgap?"AIR-GAPPED":(st.mode||"AP")),
    el("button",{class:"btn btn-danger btn-block mt",onClick:lockDevice},t("lock_now")));
  root.appendChild(hwCard);
}

async function applyOta(out){
  out.innerHTML=""; out.append(el("div",{class:"alert alert-warn",text:t("updating")}), el("div",{class:"spinner mt"}));
  try{ var r=await api("/api/ota/apply","POST",{}); toast(r.message||t("saved"),"ok"); }
  catch(e){ apiErr(e); }
}

async function lockDevice(){
  try{ await api("/api/lock","POST",{}); await boot(); }catch(e){ apiErr(e); }
}

/* ===========================================================================
 * 7) Router / boot
 * ==========================================================================*/
function render(){
  if(S.view==="connfail"){ viewConnFail(); return; }
  if(S.view==="setup"){ viewSetup(); return; }
  if(S.view==="unlock"){ viewUnlock(); return; }
  if(S.view==="dashboard"){ viewDashboard(); return; }
}

async function boot(){
  try{
    var st=await api("/api/status");
    S.status=st;
    if(st.airgap!=null) S.airgap=!!st.airgap;
    if(!st.provisioned){ S.view="setup"; updateChrome(); render(); return; }
    if(st.locked){ S.view="unlock"; updateChrome(); render(); return; }
    // unlocked -> dashboard
    S.view="dashboard";
    await loadNetworks();
    updateChrome(); render();
  }catch(e){
    S.view="connfail"; render(); apiErr(e);
  }
}

/* ===========================================================================
 * 8) Init
 * ==========================================================================*/
function init(){
  // restore language preference
  try{ var l=localStorage.getItem("dv_lang"); if(l) S.lang=l; }catch(e){}
  $("#langToggle").addEventListener("click",function(){
    S.lang = S.lang==="fa"?"en":"fa";
    try{ localStorage.setItem("dv_lang",S.lang); }catch(e){}
    applyLang();
  });
  $("#lockBtn").addEventListener("click",lockDevice);
  applyLang();       // sets dir/lang + labels
  boot();            // load status and route
}

if(document.readyState==="loading") document.addEventListener("DOMContentLoaded",init);
else init();

})();
