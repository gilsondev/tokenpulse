#pragma once
// Páginas servidas pela placa: / (configuração) e /update (atualização guiada), com o CSS em comum em
// /style.css. Os valores chegam por /api/state; tudo que vem da placa ou do GitHub entra por textContent.
#include <pgmspace.h>

#include "../web/assets.h"



static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang=pt-BR><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>tokenpulse</title>
<link rel=stylesheet href=/style.css><link rel=icon href=/logo.svg type=image/svg+xml>
<main>
<a class=brand href=/config><img src=/logo.svg alt="">tokenpulse</a>
<h1>Ajustes da placa</h1><p class=sub id=sub></p><p id=msg></p>
<section id=wifi hidden><h2>Rede WiFi</h2><p>A placa ainda não está numa rede.</p>
<div class=row><a class=btn href=/wifi>Configurar o WiFi</a></div></section>
<section><h2>Estado</h2><dl id=st></dl></section>
<form id=f>
<section><h2>Tela</h2>
<label>Brilho <input type=range name=bright min=10 max=100 step=10></label>
<label>Escurecer das 22h às 7h <input type=checkbox name=night></label>
<label>Tema <select name=theme><option value=0>segue o macOS<option value=1>claro<option value=2>escuro</select></label>
<label>Rotação <select name=rotation><option value=0>0°<option value=1>90°<option value=2>180°<option value=3>270°</select></label>
</section>
<section id=prov hidden><h2>Provedores</h2>
<label>O que a placa mostra <select name=prov><option value=auto>automático: quem teve uso nos últimos 7 dias<option value=claude>só Claude<option value=codex>só Codex<option value=both>Claude + Codex</select></label>
<p class=hint>Vale para todas as placas deste Mac. Com só Claude, a placa fica como sempre foi.</p>
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
<section><h2>Entregar a outra pessoa</h2>
<p>Deixa a placa pronta para outro Mac: atualize o firmware antes, depois prepare. Os ajustes voltam ao padrão e a placa esquece este WiFi. Na casa da pessoa, ela abre o WiFi <b id=apn>tokenpulse-…</b> para configurar a rede e mostra um QR code com a instalação no Mac (<a href="https://gilsondev.github.io/tokenpulse/comecar/" target=_blank>esta página</a>).</p>
<div class=row><button id=pp>Preparar para entrega</button></div></section>
</main>
<script>
const $=id=>document.getElementById(id),f=$('f'),say=t=>$('msg').textContent=t;
const ago=s=>s<0?'nunca':s<60?s+' s atrás':Math.floor(s/60)+' min atrás';
let host='';
async function state(){
  const s=await(await fetch('/api/state')).json();
  $('sub').textContent=s.board+' em http://'+s.host+'.local';
  $('wifi').hidden=!s.ap;
  $('apn').textContent=s.apName;
  const rows=[['Firmware',s.fw],['Agente (tokend)',s.agent||'ainda não falou com a placa'],['Endereço','http://'+s.host+'.local'],['IP',s.ip],['Rede',s.ssid?s.ssid+' ('+s.rssi+' dBm)':'desconectada'],
    ['Último dado',ago(s.frame)],['Daemon',ago(s.daemon)],['Memória livre',s.heap+' B'],['Ligada há',Math.floor(s.up/60)+' min']];
  if(s.safe)rows.unshift(['Modo','recuperação: travou 3 vezes no boot']);
  $('prov').hidden=!s.cxok;if(document.activeElement!==f.prov)f.prov.value=s.prov||'auto';
  const dl=$('st');dl.textContent='';
  for(const[k,v]of rows){const t=document.createElement('dt'),d=document.createElement('dd');t.textContent=k;
    if(/^http:\/\//.test(v)){const a=document.createElement('a');a.href=v+'/';a.textContent=v;d.append(a)}else d.textContent=v;dl.append(t,d)}
  return s;
}
state().then(s=>{
  const c=s.cfg;host=c.host;
  f.bright.value=c.bright;f.night.checked=!!c.night;f.theme.value=c.theme;f.rotation.value=c.rotation;
  f.interval.value=c.interval;f.models.checked=!!(c.pages&1);f.projects.checked=!!(c.pages&2);f.sessions.checked=!!(c.pages&4);f.detail.checked=!!(c.pages&8);f.hideCost.checked=!!c.hideCost;f.history.checked=!!(c.pages&16);f.orgs.checked=!!(c.pages&32);f.approve.checked=!!(c.alerts&1);f.band.checked=!!(c.alerts&2);f.host.value=c.host;
});
setInterval(state,5000);
f.addEventListener('change',async e=>{
  if(e.target.name==='prov'){const r=await fetch('/api/config',{method:'POST',body:new URLSearchParams({prov:f.prov.value})});return say(r.ok?'Salvo: o Mac aplica em até 2 s, em todas as placas.':await r.text())}
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
$('pp').onclick=()=>{
  if(!confirm('A placa volta ao padrão, esquece este WiFi e reinicia. Continuar?'))return;
  fetch('/api/prepare',{method:'POST'});say('Pronta para entrega. Pode desligar a placa.');
};
$('fg').onclick=()=>{
  if(!confirm('A placa esquece esta rede e abre o WiFi tokenpulse-… para você configurar outra. Continuar?'))return;
  fetch('/api/forget',{method:'POST'});say('Conecte no WiFi tokenpulse-… e abra http://192.168.4.1');
};
</script>)HTML";




