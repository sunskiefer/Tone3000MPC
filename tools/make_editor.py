#!/usr/bin/env python3
"""Build docs/PanelEditor.html: a standalone layout editor (open in any browser, no install).
Drag a control to move it, arrow keys nudge (shift = 10 px), Export writes a new layout.conf with only the moved controls' x/y changed."""
import os, sys, json, base64, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from layout_parse import parse, VIEWS
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
conf = open(os.path.join(R, "layout.conf")).read()
ctl = parse(os.path.join(R, "layout.conf"))
imgs = []
for v in range(8):
    n = "page_0.png" if v == 0 else "page_0_mode0-%d.png" % v
    imgs.append("data:image/png;base64," + base64.b64encode(open(os.path.join(R, "build/preview", n), "rb").read()).decode())
data = {"conf": conf, "views": VIEWS, "imgs": imgs, "ctl": [{k: c[k] for k in ("id", "type", "line", "view", "x", "y", "w", "h") if k in c} | {"label": c.get("label", "") or c.get("get", ""), "key": c.get("key", "")} for c in ctl]}
html = """<!doctype html><meta charset=utf-8><title>Tone3000MPC panel editor</title>
<style>body{margin:0;background:#161616;color:#eee;font:15px sans-serif}#bar{padding:8px;display:flex;gap:6px;flex-wrap:wrap;align-items:center;background:#222}
button{background:#333;color:#fff;border:1px solid #666;padding:6px 12px;font-size:15px;cursor:pointer}button.on{background:#c33}
#stage{position:relative;width:1280px;height:628px;margin:10px;background-size:100% 100%}
.c{position:absolute;box-sizing:border-box;border:2px solid #ff5050;cursor:move;font-size:13px;color:#ff9a9a;overflow:hidden}.c.sel{background:#ff505040;border-color:#ffd}
.c b{background:#ff5050;color:#000;padding:0 4px}#info{margin:0 10px 10px}</style>
<div id=bar></div><div id=stage></div><div id=info></div>
<script>
const D=@@DATA@@;let view=0,sel=null,drag=null;const st=document.getElementById('stage'),bar=document.getElementById('bar'),info=document.getElementById('info');
D.views.forEach((n,i)=>{const b=document.createElement('button');b.textContent=n;b.onclick=()=>{view=i;draw()};b.id='v'+i;bar.appendChild(b)});
const ex=document.createElement('button');ex.textContent='EXPORT layout.conf';ex.onclick=exportConf;bar.appendChild(ex);
const rs=document.createElement('button');rs.textContent='Reset';rs.onclick=()=>{D.ctl.forEach(c=>{c.x=c.x0;c.y=c.y0});draw()};bar.appendChild(rs);
D.ctl.forEach(c=>{c.x0=c.x;c.y0=c.y});
function draw(){st.style.backgroundImage='url('+D.imgs[view]+')';st.innerHTML='';D.views.forEach((n,i)=>document.getElementById('v'+i).className=i==view?'on':'');
D.ctl.filter(c=>c.view==view||c.view==-1).forEach(c=>{const e=document.createElement('div');e.className='c'+(c===sel?' sel':'');
e.style.cssText+=`left:${c.x}px;top:${c.y-86}px;width:${c.w}px;height:${c.h}px`;e.innerHTML='<b>'+c.id+'</b> '+(c.label||c.key);
e.onmousedown=ev=>{sel=c;drag={dx:ev.clientX-c.x,dy:ev.clientY-c.y};draw();ev.preventDefault()};st.appendChild(e)});
info.textContent=sel?`#${sel.id} ${sel.type} x=${sel.x} y=${sel.y} w=${sel.w} h=${sel.h} key=${sel.key} (conf line ${sel.line})`:'Click a control; drag or use arrow keys (shift = 10 px).'}
onmousemove=ev=>{if(drag&&sel){sel.x=ev.clientX-drag.dx;sel.y=ev.clientY-drag.dy;draw()}};onmouseup=()=>drag=null;
onkeydown=ev=>{if(!sel)return;const s=ev.shiftKey?10:1;({ArrowLeft:()=>sel.x-=s,ArrowRight:()=>sel.x+=s,ArrowUp:()=>sel.y-=s,ArrowDown:()=>sel.y+=s}[ev.key]||(()=>{}))();draw()};
function exportConf(){const L=D.conf.split('\\n');D.ctl.forEach(c=>{if(c.x==c.x0&&c.y==c.y0)return;let l=L[c.line-1];
 if(c.type=='list'){l=l.replace(/\\bx=-?\\d+/,'x='+c.x).replace(/\\by=-?\\d+/,'y='+c.y)}
 else{const cx=c.x+(c.type=='knob'?c.w/2:c.w/2),cy=c.y+c.h/2;l=l.replace(/\\bcx=-?\\d+/,'cx='+Math.round(cx)).replace(/\\bcy=-?\\d+/,'cy='+Math.round(cy))}L[c.line-1]=l});
 const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([L.join('\\n')],{type:'text/plain'}));a.download='layout.conf';a.click()}
draw();
</script>""".replace("@@DATA@@", json.dumps(data))
open(os.path.join(R, "docs/PanelEditor.html"), "w").write(html)
print(len(html) // 1024, "KB")
