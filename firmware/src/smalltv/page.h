#pragma once
// Páginas servidas pela placa: / (configuração) e /update (atualização guiada), com o CSS em comum em
// /style.css. Os valores chegam por /api/state; tudo que vem da placa ou do GitHub entra por textContent.
#include <pgmspace.h>

static const char STYLE[] PROGMEM = R"CSS(:root{--bg:#FAF9F5;--fg:#1F1E1D;--mut:#6B6A66;--card:#FFF;--line:#E6E3DA;--acc:#C2603F}
@media(prefers-color-scheme:dark){:root{--bg:#1F1E1D;--fg:#F5F4EE;--mut:#A3A29C;--card:#2A2927;--line:#3A3935;--acc:#D97757}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}
main{max-width:30em;margin:auto;padding:16px}
h1{font-size:20px;margin:8px 0 2px}.sub{color:var(--mut);margin:0 0 8px}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 14px;margin:12px 0}
h2{font-size:12px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut);margin:0 0 6px}
label{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:6px 0}
select,input[type=text],input[type=password]{font:inherit;padding:4px 6px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg);min-width:0}
input[type=range]{width:50%;accent-color:var(--acc)}input[type=checkbox]{accent-color:var(--acc);width:18px;height:18px}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;margin:0}dt{color:var(--mut)}dd{margin:0;text-align:right;overflow-wrap:anywhere}
button,.btn{font:inherit;padding:6px 12px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer;text-decoration:none}
.row{display:flex;gap:8px;flex-wrap:wrap}#msg{color:var(--acc);min-height:1.4em;margin:0}[hidden]{display:none}
.steps{counter-reset:s;list-style:none;padding:0;margin:0}.steps>li{counter-increment:s;position:relative;padding:2px 0 14px 40px}
.steps>li::before{content:counter(s);position:absolute;left:0;top:0;width:26px;height:26px;border-radius:50%;display:grid;place-items:center;font-size:13px;font-weight:600;background:var(--line);color:var(--fg)}
.steps>li.ok::before{content:"✓";background:#3C8A4E;color:#fff}.steps>li.bad::before{content:"!";background:#B9472F;color:#fff}
.steps h3{font-size:15px;margin:2px 0 4px}.steps p{margin:4px 0;color:var(--mut)}
.drop{border:2px dashed var(--line);border-radius:10px;padding:18px;text-align:center;color:var(--mut);cursor:pointer}.drop.on{border-color:var(--acc);color:var(--fg)}
progress{width:100%;height:10px;accent-color:var(--acc)}code{font:13px ui-monospace,monospace;background:var(--bg);padding:1px 5px;border-radius:5px}
.good{color:#3C8A4E}.warn{color:#C28A1E}.err{color:#B9472F}a{color:var(--acc)}
)CSS";

static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang=pt-BR><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>tokenpulse</title>
<link rel=stylesheet href=/style.css>
<main>
<h1>tokenpulse</h1><p class=sub id=sub></p><p id=msg></p>
<section id=wifi hidden><h2>Rede WiFi</h2>
<form method=post action=/wifi>
<label>Nome da rede <input type=text name=ssid required></label>
<label>Senha <input type=password name=pass></label>
<div class=row><button>Conectar</button></div></form></section>
<section><h2>Estado</h2><dl id=st></dl></section>
<form id=f>
<section><h2>Tela</h2>
<label>Brilho <input type=range name=bright min=10 max=100 step=10></label>
<label>Escurecer das 22h às 7h <input type=checkbox name=night></label>
<label>Tema <select name=theme><option value=0>segue o macOS<option value=1>claro<option value=2>escuro</select></label>
<label>Rotação <select name=rotation><option value=0>0°<option value=1>90°<option value=2>180°<option value=3>270°</select></label>
</section>
<section><h2>Telas</h2>
<label>Trocar de tela a cada <select name=interval><option value=0>não troca<option value=5>5 s<option value=10>10 s<option value=15>15 s<option value=30>30 s<option value=60>1 min</select></label>
<label>Sessões trabalhando e esperando você <input type=checkbox name=sessions></label>
<label>Modelos de hoje <input type=checkbox name=models></label>
<label>Detalhe de cada modelo <input type=checkbox name=detail></label>
<label>Projetos da última hora <input type=checkbox name=projects></label>
<label>Histórico de 14 dias <input type=checkbox name=history></label>
<label>Outras orgs (com mais de uma) <input type=checkbox name=orgs></label>
<label>Esconder o custo em US$ <input type=checkbox name=hideCost></label>
</section>
<section><h2>Alertas</h2>
<label>Sessão pedindo aprovação: segurar a tela de sessões e pulsar <input type=checkbox name=approve></label>
<label>Ao cruzar 70/80/90% da janela de 5 h: piscar e segurar a principal <input type=checkbox name=band></label>
</section>
<section><h2>Rede</h2>
<label>Nome na rede <span><input type=text name=host size=14 pattern="[a-z0-9\-]{1,31}">.local</span></label>
</section>
</form>
<section><h2>Sistema</h2><div class=row>
<a class=btn href=/update>Atualizar firmware</a><button id=rb>Reiniciar</button><button id=fg>Esquecer o WiFi</button>
</div></section>
</main>
<script>
const $=id=>document.getElementById(id),f=$('f'),say=t=>$('msg').textContent=t;
const ago=s=>s<0?'nunca':s<60?s+' s atrás':Math.floor(s/60)+' min atrás';
let host='';
async function state(){
  const s=await(await fetch('/api/state')).json();
  $('sub').textContent=s.board+' · '+s.fw;
  $('wifi').hidden=!s.ap;
  const rows=[['Endereço','http://'+s.host+'.local'],['IP',s.ip],['Rede',s.ssid?s.ssid+' ('+s.rssi+' dBm)':'desconectada'],
    ['Último dado',ago(s.frame)],['Daemon',ago(s.daemon)],['Memória livre',s.heap+' B'],['Ligada há',Math.floor(s.up/60)+' min']];
  if(s.safe)rows.unshift(['Modo','recuperação: travou 3 vezes no boot']);
  const dl=$('st');dl.textContent='';
  for(const[k,v]of rows){const t=document.createElement('dt'),d=document.createElement('dd');t.textContent=k;d.textContent=v;dl.append(t,d)}
  return s;
}
state().then(s=>{
  const c=s.cfg;host=c.host;
  f.bright.value=c.bright;f.night.checked=!!c.night;f.theme.value=c.theme;f.rotation.value=c.rotation;
  f.interval.value=c.interval;f.models.checked=!!(c.pages&1);f.projects.checked=!!(c.pages&2);f.sessions.checked=!!(c.pages&4);f.detail.checked=!!(c.pages&8);f.hideCost.checked=!!c.hideCost;f.history.checked=!!(c.pages&16);f.orgs.checked=!!(c.pages&32);f.approve.checked=!!(c.alerts&1);f.band.checked=!!(c.alerts&2);f.host.value=c.host;
});
setInterval(state,5000);
f.addEventListener('change',async e=>{
  if(e.target.name==='host'&&!f.host.checkValidity())return say('Nome: letras minúsculas, números e hífen.');
  const b=new URLSearchParams({bright:f.bright.value,night:+f.night.checked,theme:f.theme.value,rotation:f.rotation.value,
    interval:f.interval.value,pages:(f.models.checked?1:0)|(f.projects.checked?2:0)|(f.sessions.checked?4:0)|(f.detail.checked?8:0)|(f.history.checked?16:0)|(f.orgs.checked?32:0),hideCost:+f.hideCost.checked,alerts:(f.approve.checked?1:0)|(f.band.checked?2:0),host:f.host.value});
  const r=await fetch('/api/config',{method:'POST',body:b});
  if(!r.ok)return say(await r.text());
  const j=await r.json();
  if(j.host!==host){say('Novo endereço: http://'+j.host+'.local. Abrindo em 5 s…');setTimeout(()=>location='http://'+j.host+'.local/',5000)}
  else say('Salvo.');
  host=j.host;
});
$('rb').onclick=()=>{fetch('/api/reboot',{method:'POST'});say('Reiniciando; a página volta em ~15 s.')};
$('fg').onclick=()=>{
  if(!confirm('A placa esquece esta rede e abre o WiFi tokenpulse-… para você configurar outra. Continuar?'))return;
  fetch('/api/forget',{method:'POST'});say('Conecte no WiFi tokenpulse-… e abra http://192.168.4.1');
};
</script>)HTML";


static const char UPDATE_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang=pt-BR><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Atualizar · tokenpulse</title><link rel=stylesheet href=/style.css>
<main>
<p><a href=/>← configuração</a></p>
<h1>Atualizar o firmware</h1><p class=sub id=sub>lendo a placa…</p>
<section><h2>Versões</h2><dl id=ver></dl><p id=verMsg></p></section>
<section><h2>Passo a passo</h2><ol class=steps>
<li id=s1><h3>Baixe o firmware</h3>
<p>Baixe o <a id=bin href="https://github.com/gilsondev/tokenpulse/releases/latest/download/tokenpulse-smalltv-ultra-ota.bin">tokenpulse-smalltv-ultra-ota.bin</a> do <a href="https://github.com/gilsondev/tokenpulse/releases/latest" target=_blank>último release</a>. Use só o arquivo do <b>smalltv-ultra</b>: o das outras placas não roda aqui.</p>
<p>Compilou da fonte? Use <code>firmware/.pio/build/smalltv-ultra/firmware.bin</code>.</p></li>
<li id=s2><h3>Escolha o arquivo</h3>
<label class=drop id=drop><input type=file id=file accept=".bin,.gz" hidden><span id=fileMsg>Toque para escolher ou arraste o .bin aqui</span></label></li>
<li id=s3><h3>Envie para a placa</h3>
<p>Leva uns 10 s. Não feche a página e não desligue a placa.</p>
<div class=row><button id=send disabled>Enviar firmware</button></div>
<p><progress id=bar max=100 value=0 hidden></progress></p><p id=sendMsg></p></li>
<li id=s4><h3>Confira se deu certo</h3>
<p id=doneMsg>A placa reinicia sozinha. Esta página espera ela voltar e confere a versão.</p></li>
</ol>
<p>Se algo falhar no meio, a placa continua na versão atual: ela só troca depois de gravar e conferir o arquivo inteiro. Se o firmware novo travar 3 vezes seguidas, ela volta só com WiFi e esta página, para você mandar outro.</p>
</section>
<section><h2>Pelo Mac</h2><p>Com o daemon rodando, dá para atualizar sem o navegador: <code>./update.sh</code> manda o último release, e <code>firmware/flash.sh smalltv-ultra</code> compila e manda.</p></section>
</main>
<script>
const $=id=>document.getElementById(id),mark=(id,c)=>{$(id).className=c};
const say=(id,t,c)=>{$(id).textContent=t;$(id).className=c||''};
let before=null,room=0,file=null;
async function state(t){const c=new AbortController();setTimeout(()=>c.abort(),t||3000);
  return(await fetch('/api/state',{cache:'no-store',signal:c.signal})).json()}
function row(k,v){const t=document.createElement('dt'),d=document.createElement('dd');t.textContent=k;d.textContent=v;$('ver').append(t,d)}
state().then(s=>{before=s.fw;room=s.ota;$('sub').textContent=s.board+' em http://'+s.host+'.local';
  row('Na placa',s.fw);row('Espaço para o novo',Math.floor(s.ota/1024)+' KB');
  fetch('https://api.github.com/repos/gilsondev/tokenpulse/releases/latest').then(r=>r.json()).then(j=>{
    row('Último release',j.tag_name);
    if(s.fw===j.tag_name){say('verMsg','A placa já está no último release.','good');mark('s1','ok')}
    else if(s.fw.startsWith('dev'))say('verMsg','A placa roda um firmware compilado localmente. O release pode ser mais antigo que ele.','warn');
    else say('verMsg','Há uma versão nova: siga os passos abaixo.','warn');
  }).catch(()=>say('verMsg','Não consegui consultar o GitHub agora. O link do passo 1 continua valendo.','warn'));
}).catch(()=>say('sub','Não consegui ler a placa. Recarregue a página.','err'));
function pick(f){
  file=null;$('send').disabled=true;mark('s2','');
  if(!f)return;
  if(!/\.bin(\.gz)?$/i.test(f.name))return say('fileMsg',f.name+': escolha um arquivo .bin','err'),mark('s2','bad');
  if(/tokenpulse-/.test(f.name)&&!/smalltv-ultra/.test(f.name))return say('fileMsg',f.name+' é de outra placa. Use o tokenpulse-smalltv-ultra-ota.bin.','err'),mark('s2','bad');
  if(room&&f.size>room)return say('fileMsg',f.name+' tem '+Math.round(f.size/1024)+' KB e não cabe ('+Math.floor(room/1024)+' KB livres).','err'),mark('s2','bad');
  file=f;say('fileMsg',f.name+' · '+Math.round(f.size/1024)+' KB','good');mark('s2','ok');mark('s1','ok');$('send').disabled=false;
}
$('file').onchange=e=>pick(e.target.files[0]);
const drop=$('drop');
drop.ondragover=e=>{e.preventDefault();drop.classList.add('on')};drop.ondragleave=()=>drop.classList.remove('on');
drop.ondrop=e=>{e.preventDefault();drop.classList.remove('on');pick(e.dataTransfer.files[0])};
$('send').onclick=()=>{
  if(!file)return;$('send').disabled=true;$('bar').hidden=false;say('sendMsg','Enviando…');
  const x=new XMLHttpRequest(),fd=new FormData();fd.append('firmware',file,file.name);
  x.upload.onprogress=e=>{if(e.lengthComputable)$('bar').value=Math.round(100*e.loaded/e.total)};
  x.onload=()=>{
    if(/Update Success/.test(x.responseText)){$('bar').value=100;say('sendMsg','Gravado e conferido. A placa está reiniciando…','good');mark('s3','ok');wait()}
    else{say('sendMsg','A placa recusou: '+x.responseText.replace(/<[^>]*>/g,'').trim()+'. Ela continua na versão atual.','err');mark('s3','bad');$('send').disabled=false}
  };
  x.onerror=()=>{say('sendMsg','A conexão caiu no meio do envio. A placa continua na versão atual; tente de novo.','err');mark('s3','bad');$('send').disabled=false};
  x.open('POST','/update');x.send(fd);
};
async function wait(){
  const t0=Date.now();await new Promise(r=>setTimeout(r,4000));
  while(Date.now()-t0<90000){
    try{const s=await state(2000);
      if(s.safe){say('doneMsg','A placa voltou em modo de recuperação: o firmware novo travou ao iniciar. Mande outro arquivo pelo passo 2.','err');mark('s4','bad');return}
      if(s.fw!==before){say('doneMsg','Pronto: a placa voltou com '+s.fw+'.','good');mark('s4','ok');return}
      say('doneMsg','A placa voltou, mas com a mesma versão ('+s.fw+'). O arquivo enviado pode ser o mesmo que já estava nela.','warn');mark('s4','ok');return
    }catch(e){say('doneMsg','Esperando a placa voltar… '+Math.round((Date.now()-t0)/1000)+' s')}
    await new Promise(r=>setTimeout(r,2000));
  }
  say('doneMsg','A placa não respondeu em 90 s. Confira se ela está ligada e na mesma rede, e recarregue esta página.','err');mark('s4','bad');
}
</script>)HTML";
