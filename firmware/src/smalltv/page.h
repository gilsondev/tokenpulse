#pragma once
// Página de configuração servida em /. Os valores chegam por /api/state e cada mudança vai para
// /api/config na hora, sem botão de salvar. Tudo que vem da placa entra por textContent.
#include <pgmspace.h>

static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang=pt-BR><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>tokenpulse</title>
<style>
:root{--bg:#FAF9F5;--fg:#1F1E1D;--mut:#6B6A66;--card:#FFF;--line:#E6E3DA;--acc:#C2603F}
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
</style>
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
<label>Esconder o custo em US$ <input type=checkbox name=hideCost></label>
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
  f.interval.value=c.interval;f.models.checked=!!(c.pages&1);f.projects.checked=!!(c.pages&2);f.sessions.checked=!!(c.pages&4);f.detail.checked=!!(c.pages&8);f.hideCost.checked=!!c.hideCost;f.host.value=c.host;
});
setInterval(state,5000);
f.addEventListener('change',async e=>{
  if(e.target.name==='host'&&!f.host.checkValidity())return say('Nome: letras minúsculas, números e hífen.');
  const b=new URLSearchParams({bright:f.bright.value,night:+f.night.checked,theme:f.theme.value,rotation:f.rotation.value,
    interval:f.interval.value,pages:(f.models.checked?1:0)|(f.projects.checked?2:0)|(f.sessions.checked?4:0)|(f.detail.checked?8:0),hideCost:+f.hideCost.checked,host:f.host.value});
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
