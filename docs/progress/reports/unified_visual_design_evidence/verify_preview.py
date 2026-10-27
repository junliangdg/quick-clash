from pathlib import Path
import re,json,hashlib,subprocess
root=Path(__file__).resolve().parents[4]
out=root/'docs/progress/reports/unified_visual_design_evidence'
s=(root/'docs/reference/quickclash_minimal_preview.fragment.html').read_text(encoding='utf-8')
js=re.findall(r'<script>([\s\S]*?)</script>',s)
assert len(js)==1
result=subprocess.run(['node','-e','const vm=require("vm");new vm.Script(require("fs").readFileSync(0,"utf8"));console.log("Preview JavaScript syntax passed.");'],input=js[0],text=True,capture_output=True)
assert result.returncode==0,result.stderr
assert not re.search(r'<(?:script|link|img|iframe)[^>]*(?:src|href)\s*=\s*["\']https?://',s,re.I)
assert not re.search(r'\b(?:fetch\s*\(|XMLHttpRequest|WebSocket|url\s*\(|@import)',s)
assert 'data-design="material"' not in s and 'qc-design' not in s
assert s.count('data-design="minimal"][data-appearance="dark"]')==1
# Check every text/state pairing prescribed by the design, including selected hover states.
def lum(c):
    vals=[int(c[i:i+2],16)/255 for i in (1,3,5)]
    vals=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in vals]
    return sum(a*b for a,b in zip(vals,[.2126,.7152,.0722]))
def contrast(a,b):
    x,y=sorted((lum(a),lum(b)));return (y+.05)/(x+.05)
palettes={
'light':dict(bg='#FAFAFB',nav='#F4F4F6',surface='#FFFFFF',inset='#F2F2F5',text='#202024',muted='#62626E',border='#858591',primary='#5B4BC4',onPrimary='#FFFFFF',pHover='#5141B2',pPressed='#46369F',selection='#EEEBFD',onSelection='#5340AB',sHover='#E5DFFB',sPressed='#DAD2F6',hover='#EAEAF0',pressed='#E0DFE8',success='#246944',successBg='#EAF5EE',warning='#855510',warningBg='#FFF4DF',error='#B33043',errorBg='#FFF0F2',eHover='#FBE3E8',ePressed='#F8D8DF',upload='#936025'),
'dark':dict(bg='#101114',nav='#15161A',surface='#191A20',inset='#22242C',text='#EDEDF2',muted='#A8A9B8',border='#818496',primary='#B0A5FF',onPrimary='#251A58',pHover='#BDB3FF',pPressed='#A195F0',selection='#312A4F',onSelection='#C9BFFF',sHover='#3B335F',sPressed='#453B6D',hover='#2A2C36',pressed='#343743',success='#93D4AC',successBg='#1D3228',warning='#EBC47E',warningBg='#382D1C',error='#FFA0B0',errorBg='#3B222C',eHover='#4A2A35',ePressed='#59323F',upload='#D5AE72')}
checks=[]
for theme,p in palettes.items():
    pairs=[]
    for fg in ['text','muted']:
        for bg in ['bg','nav','surface','inset','hover','pressed','selection']:
            pairs.append((fg,bg,4.5))
    for bg in ['primary','pHover','pPressed']: pairs.append(('onPrimary',bg,4.5))
    for bg in ['selection','sHover','sPressed']: pairs.append(('onSelection',bg,4.5))
    for fg,bg in [('success','successBg'),('warning','warningBg'),('error','errorBg'),('error','eHover'),('error','ePressed')]:pairs.append((fg,bg,4.5))
    for fg in ['success','warning','error','upload']:
        pairs.append((fg,'surface',4.5))
    for fg in ['border','primary']:
        for bg in ['bg','nav','surface','inset']: pairs.append((fg,bg,3))
    for fg,bg,minimum in pairs:
        ratio=contrast(p[fg],p[bg]);checks.append(dict(theme=theme,foreground=fg,background=bg,ratio=round(ratio,3),minimum=minimum,pass_=ratio>=minimum))
failures=[x for x in checks if not x['pass_']]
(out/'contrast_checks.json').write_text(json.dumps(dict(checks=checks,failures=failures),ensure_ascii=False,indent=2),encoding='utf-8')
print(result.stdout.strip())
print('Offline resource/API check passed; fixed option C; one palette per theme.')
print(f'Contrast checks: {len(checks)} pairings, {len(failures)} failures.')
for failure in failures: print(failure)
assert not failures
files=['docs/design/17-界面参考与QtQuick落地.md','docs/reference/quickclash_minimal_preview.fragment.html','docs/reference/quickclash_minimal_preview.html','docs/reference/quickclash_minimal_light.png','docs/reference/quickclash_minimal_dark.png']
(out/'asset_sha256.json').write_text(json.dumps({p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in files},ensure_ascii=False,indent=2),encoding='utf-8')

