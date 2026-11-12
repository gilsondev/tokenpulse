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
<section id=mask class=ask hidden><h2>Multica</h2>
<p>Achei o Multica neste computador. Mostrar a página do Multica nas placas?</p>
<div class=row><button type=button class=pri data-mc=1>Mostrar</button><button type=button data-mc=0>Agora não</button></div>
<p class=hint>Seus agentes, issues e projetos. Dá para mudar depois em Multica, mais abaixo.</p></section>
<section id=lask class=ask hidden><h2>Login do Claude</h2>
<p>Sem o login, a placa não mostra o % de uso. No terminal, rode <code>claude</code> e entre na sua conta. A placa segue sozinha.</p>
<div class=row><button type=button id=lskip>Pular: só tokens, sem %</button></div></section>
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
<section id=updsec class=ask hidden><h2>Versão nova</h2>
<p id=updtxt></p>
<div class=row><button id=updbtn class=pri type=button>Atualizar agora</button></div>
<p class=hint>Atualiza o programa do computador e depois o firmware das placas, em cerca de 1 min.</p>
</section>
<section id=prov hidden><h2>Provedores</h2>
<p id=pq class=hint hidden></p>
<label>Automático: quem teve uso nos últimos 7 dias <input type=checkbox name=pauto></label>
<label id=pcl>Claude <input type=checkbox name=pcl></label>
<label id=pcx>Codex <input type=checkbox name=pcx></label>
<p class=hint>Vale para todas as placas deste computador. Com só Claude, a placa fica como sempre foi.</p>
</section>
<section id=mcsec hidden><h2>Multica</h2>
<label>Página do Multica <select name=mc><option value=1>mostrar<option value=0>ocultar</select></label>
<label>Workspace <select name=mcws></select></label>
<p class=hint>Mostra só o seu trabalho: os seus agentes, as suas issues (com as dos seus squads) e os projetos em que você está. Vale para todas as placas deste computador.</p>
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
<p>Deixa a placa pronta para outro computador: atualize o firmware antes, depois prepare. Os ajustes voltam ao padrão e a placa esquece este WiFi. Na casa da pessoa, ela abre o WiFi <b id=apn>tokenpulse-caixeta</b> para configurar a rede e mostra um QR code com a instalação no computador (<a href="https://gilsondev.github.io/tokenpulse/comecar/" target=_blank>esta página</a>).</p>
<div class=row><button id=pp>Preparar para entrega</button></div></section>
</main>
<script>
const $=id=>document.getElementById(id),f=$('f'),say=t=>$('msg').textContent=t;
const ago=s=>s<0?'nunca':s<60?s+' s atrás':Math.floor(s/60)+' min atrás';
let host='',provAt=0;
// Provedores: auto, claude, codex ou both, em três chaves; com o automático ligado, as outras duas somem
const provShow=v=>{f.pauto.checked=v==='auto';f.pcl.checked=v==='claude'||v==='both';f.pcx.checked=v==='codex'||v==='both';$('pcl').hidden=$('pcx').hidden=v==='auto'};
const provVal=e=>{if(!f.pauto.checked&&!f.pcl.checked&&!f.pcx.checked)(e.target===f.pcl?f.pcx:f.pcl).checked=true;
  return f.pauto.checked?'auto':f.pcl.checked&&f.pcx.checked?'both':f.pcx.checked?'codex':'claude'};
async function state(){
  const s=await(await fetch('/api/state')).json();
  $('sub').textContent=(s.nick?s.nick+' · ':'')+s.board+' em http://'+s.host+'.local';
  $('wifi').hidden=!s.ap;
  $('apn').textContent=s.apName;
  const rows=[['Firmware',s.fw],['Agente (tokend)',s.agent||'ainda não falou com a placa'],['Endereço','http://'+s.host+'.local'],['IP',s.ip],['Rede',s.ssid?s.ssid+' ('+s.rssi+' dBm)':'desconectada'],
    ['Último dado',ago(s.frame)],['Daemon',ago(s.daemon)],['Memória livre',s.heap+' B'],['Ligada há',Math.floor(s.up/60)+' min']];
  if(s.safe)rows.unshift(['Modo','recuperação: travou 3 vezes no boot']);
  $('prov').hidden=!s.cxok;if(Date.now()-provAt>4000)provShow(s.prov||'auto');
  $('updsec').hidden=!s.upd;if(s.upd)$('updtxt').textContent=s.fw+' → '+s.upd;
  const m=s.mcs;$('mcsec').hidden=!m;
  if(m){if(f.mcws.options.length!==m.w.length){f.mcws.textContent='';m.w.forEach((n,i)=>f.mcws.add(new Option(n,i)))}
    for(const[k,v]of[['mc',m.on],['mcws',m.i]])if(document.activeElement!==f[k])f[k].value=v}
  // primeiro uso: uma pergunta por vez, na ordem das placas com toque (provedores, Multica, login, versão nova)
  const step=s.pask?'prov':s.mask?'mask':s.lask?'lask':s.upd?'updsec':'';
  for(const id of['mask','lask'])$(id).hidden=step!==id;
  for(const id of['prov','mask','lask','updsec'])$(id).classList.toggle('now',id===step);
  if(step==='prov')f.prepend($(step));else if(step)f.before($(step));  // o Provedores é campo do formulário
  const ask=$('prov'),q=$('pq');q.hidden=!s.pask;
  if(s.pask){q.textContent=(s.pask==='codex'?'Este computador usa o Codex.':'Este computador usa o Claude e o Codex.')+' Escolha o que as placas mostram:'}
  const dl=$('st');dl.textContent='';
  for(const[k,v]of rows){const t=document.createElement('dt'),d=document.createElement('dd');t.textContent=k;
    if(/^http:\/\//.test(v)){const a=document.createElement('a');a.href=v+'/';a.textContent=v;d.append(a)}else d.textContent=v;{const b=document.createElement('div');if(String(v).length>16)b.className='w';b.append(t,d);dl.append(b)}}
  return s;
}
state().then(s=>{
  const c=s.cfg;host=c.host;
  f.bright.value=c.bright;f.night.checked=!!c.night;f.theme.value=c.theme;f.rotation.value=c.rotation;
  f.interval.value=c.interval;f.models.checked=!!(c.pages&1);f.projects.checked=!!(c.pages&2);f.sessions.checked=!!(c.pages&4);f.detail.checked=!!(c.pages&8);f.hideCost.checked=!!c.hideCost;f.history.checked=!!(c.pages&16);f.orgs.checked=!!(c.pages&32);f.approve.checked=!!(c.alerts&1);f.band.checked=!!(c.alerts&2);f.host.value=c.host;
});
$('updbtn').onclick=async()=>{const r=await fetch('/api/config',{method:'POST',body:new URLSearchParams({update:1})});say(r.ok?'Atualizando: o computador baixa a versão nova e grava as placas. Não desligue.':await r.text())};
const act=async(b,ok)=>{const r=await fetch('/api/config',{method:'POST',body:new URLSearchParams(b)});say(r.ok?ok:await r.text());state()};
document.querySelectorAll('[data-mc]').forEach(b=>b.onclick=()=>act({mc:b.dataset.mc},'Salvo: o computador aplica em até 2 s, em todas as placas.'));
$('lskip').onclick=()=>act({lskip:1},'Combinado: a placa mostra só os tokens.');
setInterval(state,5000);
f.addEventListener('change',async e=>{
  if(['mc','mcws'].includes(e.target.name)){const r=await fetch('/api/config',{method:'POST',body:new URLSearchParams({[e.target.name]:e.target.value})});return say(r.ok?'Salvo: o computador aplica em até 2 s, em todas as placas.':await r.text())}
  if(['pauto','pcl','pcx'].includes(e.target.name)){const v=provVal(e);provAt=Date.now();provShow(v);const r=await fetch('/api/config',{method:'POST',body:new URLSearchParams({prov:v})});return say(r.ok?'Salvo: o computador aplica em até 2 s, em todas as placas.':await r.text())}
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
  if(!confirm('A placa esquece esta rede e abre o WiFi tokenpulse-caixeta para você configurar outra. Continuar?'))return;
  fetch('/api/forget',{method:'POST'});say('Conecte no WiFi tokenpulse-caixeta e abra http://192.168.4.1');
};
</script>)HTML";




