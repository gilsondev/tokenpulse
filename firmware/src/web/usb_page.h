#pragma once
// Ajustes das placas USB (Guition, ESP32-2432S028R, Waveshare): os da tela e os que só a página tem (troca
// automática de tela, telas da volta, alerta, nome na rede). Cada mudança vai para /api/config na hora.
#include <pgmspace.h>

static const char USB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang=pt-BR><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Ajustes · tokenpulse</title><link rel=stylesheet href=/style.css><link rel=icon href=/logo.svg type=image/svg+xml>
<main>
<a class=brand href=/config><img src=/logo.svg alt="">tokenpulse</a>
<h1>Ajustes da placa</h1><p class=sub id=sub></p><p id=msg></p>
<section><h2>Estado</h2><dl id=st></dl></section>
<form id=f>
<section><h2>Tela</h2>
<label>Brilho <select name=bright><option value=0>25%<option value=1>50%<option value=2>75%<option value=3>100%</select></label>
<label>Escurecer quando ninguém toca <select name=idle><option value=0>depois de 5 min<option value=1>depois de 10 min<option value=2>depois de 30 min<option value=3>nunca</select></label>
<label>Tema <select name=theme><option value=0>segue o macOS<option value=1>claro<option value=2>escuro</select></label>
<label id=orow>Tela <select name=orient><option value=0>em pé<option value=1>deitada<option value=2 hidden>em pé, de cabeça para baixo<option value=3 hidden>deitada, virada</select></label>
<label>Esconder o custo em US$ <input type=checkbox name=hideCost></label>
</section>
<section id=prov hidden><h2>Provedores</h2>
<label>O que a placa mostra <select name=prov><option value=auto>automático: quem teve uso nos últimos 7 dias<option value=claude>só Claude<option value=codex>só Codex<option value=both>Claude + Codex</select></label>
<p class=hint>Vale para todas as placas deste Mac. Com só Claude, a placa fica como sempre foi.</p>
</section>
<section><h2>Telas</h2>
<label>Trocar de tela sozinha <select name=interval><option value=0>não, só pelo toque<option value=10>a cada 10 s<option value=15>a cada 15 s<option value=30>a cada 30 s<option value=60>a cada 1 min</select></label>
<p class=hint>A troca automática só acontece quando ninguém mexe na placa: um toque recomeça a contagem.</p>
<label>Sessões <input type=checkbox name=sessions></label>
<label>Modelos de hoje <input type=checkbox name=models></label>
<label>Histórico de 14 dias <input type=checkbox name=history></label>
</section>
<section id=rest hidden><h2>Descanso</h2>
<p class=hint>Parada, a tela OLED troca o painel por versões resumidas, que mudam de lugar para não marcar o vidro.</p>
<label>Relógio <select name=restSecs><option value=0>HH:MM<option value=1>HH:MM:SS</select></label>
<label>Relógio <input type=checkbox name=rp0></label>
<label>Uso (5h e semana) <input type=checkbox name=rp1></label>
<label>Sessões <input type=checkbox name=rp2></label>
<label>Modelos de hoje <input type=checkbox name=rp3></label>
<label>Histórico de 14 dias <input type=checkbox name=rp4></label>
</section>
<section><h2>Alertas</h2>
<label>Sessão pedindo aprovação: abrir as sessões e pulsar o brilho <input type=checkbox name=alerts></label>
<label id=srow hidden>Sessão pedindo aprovação: tocar um som <input type=checkbox name=beep></label>
<label id=prow hidden>Sessão esperando você <span><select name=sound id=snd></select> <button id=play type=button>ouvir</button></span></label>
<label id=orow2 hidden>Janela de 5h esgotou <select name=soundOut></select></label>
<label id=nrow hidden>Janela de 5h renovou <select name=soundNew></select></label>
<label id=wrow hidden>Limite da semana esgotou <select name=soundWeek></select></label>
<label id=brow hidden>Bateria fraca <select name=soundBat></select></label>
<label id=mrow hidden>Mac sem dados há 1 min <select name=soundMac></select></label>
<label id=lrow hidden>Bluetooth conectou ou caiu <select name=soundBle></select></label>
<label id=vrow hidden>Volume <select name=volume><option value=1>baixo<option value=2>médio<option value=3>alto</select></label>
</section>
<section><h2>Rede</h2>
<label>Nome na rede <span><input type=text name=host size=14 pattern="[a-z0-9\-]{1,31}">.local</span></label>
<div class=row><button id=fg type=button>Esquecer o WiFi</button></div>
<p class=hint>Sem WiFi a placa continua funcionando pelo cabo; a página volta quando você ligar o WiFi de novo em Ajustes → Sobre → página web.</p>
</section>
</form>
<section><h2>Entregar a outra pessoa</h2>
<p>Atualize o firmware antes, depois prepare. Os ajustes voltam ao padrão e a placa esquece este WiFi; a calibração do toque fica. Ligada no Mac da pessoa, a tela mostra um QR code com a instalação (<a href="https://tokenpulse.s.gy/comecar" target=_blank>esta página</a>).</p>
<div class=row><button id=pp type=button>Preparar para entrega</button></div></section>
<section><h2>Sistema</h2><div class=row>
<a class=btn href=/update>Atualizar firmware</a><button id=rb type=button>Reiniciar</button>
</div></section>
</main>
<script>
const $=id=>document.getElementById(id),f=$('f'),say=t=>$('msg').textContent=t;
const ago=s=>s<0?'nunca':s<60?s+' s atrás':Math.floor(s/60)+' min atrás';
let host='';
async function state(){
  const s=await(await fetch('/api/state',{cache:'no-store'})).json();
  $('sub').textContent=s.board+' em http://'+s.host+'.local';
  $('orow').hidden=s.imu;$('rest').hidden=!s.oled;$('srow').hidden=$('prow').hidden=$('vrow').hidden=$('orow2').hidden=$('nrow').hidden=$('wrow').hidden=$('brow').hidden=$('mrow').hidden=$('lrow').hidden=!s.speaker;
  if(s.sounds&&!f.sound.options.length){s.sounds.forEach((n,i)=>f.sound.add(new Option(n,i)));for(const k of['soundOut','soundNew','soundWeek','soundBat','soundMac','soundBle']){s.sounds.forEach((n,i)=>f[k].add(new Option(n,i)));f[k].add(new Option('desligado',s.sounds.length))}}
  for(const o of f.orient.options)if(o.value>1)o.hidden=s.rotations<4;
  const rows=[['Firmware',s.fw],['Agente (tokend)',s.agent||'ainda não falou com a placa'],['Endereço','http://'+s.host+'.local'],
    ['IP',s.ip||'—'],['Rede',s.ssid?s.ssid+' ('+s.rssi+' dBm)':'sem WiFi'],['Último dado',ago(s.frame)],
    ['Memória livre',Math.round(s.heap/1024)+' KB'],['Ligada há',Math.floor(s.up/60)+' min']];
  $('prov').hidden=!s.cxok;if(document.activeElement!==f.prov)f.prov.value=s.prov||'auto';
  const dl=$('st');dl.textContent='';
  for(const[k,v]of rows){const t=document.createElement('dt'),d=document.createElement('dd');t.textContent=k;
    if(/^http:\/\//.test(v)){const a=document.createElement('a');a.href=v+'/';a.textContent=v;d.append(a)}else d.textContent=v;dl.append(t,d)}
  return s;
}
state().then(s=>{
  const c=s.cfg;host=c.host;
  for(const k of['bright','idle','theme','orient','interval'])f[k].value=c[k];
  f.hideCost.checked=!!c.hideCost;f.alerts.checked=!!(c.alerts&1);f.beep.checked=!!(c.alerts&2);f.sound.value=c.sound;f.volume.value=c.volume;f.soundOut.value=c.soundOut;f.soundNew.value=c.soundNew;f.soundWeek.value=c.soundWeek;f.soundBat.value=c.soundBat;f.soundMac.value=c.soundMac;f.soundBle.value=c.soundBle;f.restSecs.value=c.restSecs;for(let i=0;i<5;i++)f['rp'+i].checked=!!(c.restPages>>i&1);f.host.value=c.host;
  f.sessions.checked=!!(c.pages&1);f.models.checked=!!(c.pages&2);f.history.checked=!!(c.pages&4);
});
setInterval(state,5000);
f.addEventListener('change',async e=>{
  if(e.target.name==='prov'){const r=await fetch('/api/config',{method:'POST',body:new URLSearchParams({prov:f.prov.value})});return say(r.ok?'Salvo: vale para todas as placas deste Mac.':await r.text())}
  if(e.target.name==='host'&&!f.host.checkValidity())return say('Nome: letras minúsculas, números e hífen.');
  const b=new URLSearchParams({bright:f.bright.value,idle:f.idle.value,theme:f.theme.value,orient:f.orient.value,
    hideCost:+f.hideCost.checked,interval:f.interval.value,alerts:(f.alerts.checked?1:0)|(f.beep.checked?2:0),sound:f.sound.value||0,volume:f.volume.value||2,soundOut:f.soundOut.value||0,soundNew:f.soundNew.value||0,soundWeek:f.soundWeek.value||0,soundBat:f.soundBat.value||0,soundMac:f.soundMac.value||0,soundBle:f.soundBle.value||0,restSecs:f.restSecs.value||0,restPages:[0,1,2,3,4].reduce((m,i)=>m|(f['rp'+i].checked?1<<i:0),0)||1,host:f.host.value,
    pages:(f.sessions.checked?1:0)|(f.models.checked?2:0)|(f.history.checked?4:0)});
  const r=await fetch('/api/config',{method:'POST',body:b});
  if(!r.ok)return say(await r.text());
  const j=await r.json();
  if(j.host!==host){say('Novo endereço: http://'+j.host+'.local. Abrindo em 5 s…');setTimeout(()=>location='http://'+j.host+'.local/',5000)}
  else say('Salvo.');
  host=j.host;
});
$('play').onclick=()=>fetch('/api/play',{method:'POST',body:new URLSearchParams({i:f.sound.value})});
$('rb').onclick=()=>{fetch('/api/reboot',{method:'POST'});say('Reiniciando; a página volta em ~15 s.')};
$('fg').onclick=()=>{if(!confirm('A placa esquece esta rede e desliga o WiFi. Continuar?'))return;fetch('/api/forget',{method:'POST'});say('WiFi esquecido. A placa segue pelo cabo.')};
$('pp').onclick=()=>{if(!confirm('A placa volta ao padrão, esquece este WiFi e reinicia. Continuar?'))return;fetch('/api/prepare',{method:'POST'});say('Pronta para entrega. Pode desplugar a placa.')};
</script>)HTML";
