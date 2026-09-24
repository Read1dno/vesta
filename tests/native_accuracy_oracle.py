from pathlib import Path
import ctypes as c,os,pefile,hashlib,json,random,struct,math
import argparse
parser=argparse.ArgumentParser(description='Private-process oracle for the audited client version; never opens the game process.')
parser.add_argument('--client',required=True,type=Path)
parser.add_argument('--runtime-bin',required=True,type=Path)
parser.add_argument('--output-dir',required=True,type=Path)
args=parser.parse_args();dll=args.client.resolve(strict=True);a=args.output_dir.resolve();a.mkdir(parents=True,exist_ok=True)
expected='40bce8206f51b92ee05d6121c6e42c717bf3fa0cc0edeeb6698744b1c4799feb'
if hashlib.sha256(dll.read_bytes()).hexdigest()!=expected:raise RuntimeError('Unreviewed DLL version; do not reuse these RVAs')
k=c.WinDLL('kernel32',use_last_error=True)
k.LoadLibraryExW.argtypes=[c.c_wchar_p,c.c_void_p,c.c_uint];k.LoadLibraryExW.restype=c.c_void_p
k.VirtualProtect.argtypes=[c.c_void_p,c.c_size_t,c.c_uint,c.POINTER(c.c_uint)];k.VirtualProtect.restype=c.c_int
folder=args.runtime_bin.resolve(strict=True)
with os.add_dll_directory(str(folder)): tier=c.CDLL(str(folder/'tier0.dll'))
base=k.LoadLibraryExW(str(dll),None,1)
if not base:raise OSError(c.get_last_error())
p=pefile.PE(str(dll),fast_load=True);p.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
bound=[]
for desc in p.DIRECTORY_ENTRY_IMPORT:
 module=tier if desc.dll.lower()==b'tier0.dll' else k if desc.dll.lower()==b'kernel32.dll' else None
 if module is None:continue
 for entry in desc.imports:
  if not entry.name:continue
  try:address=c.cast(getattr(module,entry.name.decode()),c.c_void_p).value
  except AttributeError:continue
  slot=base+entry.address-p.OPTIONAL_HEADER.ImageBase;old=c.c_uint();assert k.VirtualProtect(slot,8,4,c.byref(old))
  c.c_void_p.from_address(slot).value=address;temp=c.c_uint();k.VirtualProtect(slot,8,old.value,c.byref(temp));bound.append(entry.name.decode())
# All allocations and bindings below belong exclusively to this Python process.
keep=[]
def alloc(n):
 b=c.create_string_buffer(n);keep.append(b);return c.addressof(b)
def put(addr,fmt,*values):c.memmove(addr,struct.pack('<'+fmt,*values),struct.calcsize('<'+fmt))
def cv(rva,value,fmt='f'):
 obj=alloc(0x80);put(obj+0x58,fmt,value);put(base+rva,'Q',obj);return obj
force=cv(0x24bf470,0.);no=cv(0x24bf480,0,'B');air=cv(0x24bf490,1.);jump=cv(0x2527d58,301.993377)
patterns=cv(0x25b4550,0,'B');strafe=cv(0x24bf5a0,0,'B');bias=cv(0x24bf5b0,.5);scale=cv(0x24bf5c0,.1)
# EyeAngles selects the plain stored-angle branch; its interpolation is disabled in this fixture.
put(base+0x25324e0,'i',1)
weapon=alloc(0x2200);pawn=alloc(0x4000);vdata=alloc(0x1000);wv=alloc(0xe00);pv=alloc(0x800);node=alloc(0x180)
put(weapon,'Q',wv);put(pawn,'Q',pv);put(weapon+0x388,'Q',vdata);put(pawn+0x330,'Q',node)
put(wv+0xba0,'Q',base+0x7ce9c0);put(wv+0xb70,'Q',base+0x7ce270);put(pv+0x5c8,'Q',base+0xba80e0);put(pv+0x570,'Q',base+0xc73910)
chunk=alloc(512*112);directory=alloc(64*8);put(directory,'Q',chunk);put(base+0x23652f0,'Q',directory)
put(chunk+112,'Q',pawn);put(chunk+112+16,'I',1);put(chunk+224,'Q',node);put(chunk+224+16,'I',2)
put(weapon+0x520,'I',1);put(pawn+0x526,'B',2)
fn=c.CFUNCTYPE(c.c_float,c.c_void_p,c.c_void_p,c.c_void_p)(base+0x80c300)
rng=random.Random(24092026)
rows=[]
for i in range(8192):
 v=[rng.uniform(-320,320),rng.uniform(-320,320),rng.uniform(-600,400)] if i>=16 else [0.,float(i)*20,(-1)**i*float(i)*35]
 angles=[rng.uniform(-89,89),rng.uniform(-180,180),0.]
 speed=rng.choice([0.,150.,220.,240.,250.]);move=rng.uniform(0,.1);penalty=rng.uniform(0,.15);turning=rng.uniform(0,.06)
 initial=rng.uniform(0,.4);apex=rng.uniform(0,.08);airscale=rng.choice([0.,.5,1.,2.]);impulse=rng.choice([0.,200.,301.993377,400.])
 forcespread=rng.choice([0.,0.,0.,.1,2.]) if i%31==0 else 0.;nospread=(i%67==0);walking=i%2;grounded=i%3==0;strafing=i%5==0
 sbias=rng.choice([.1,.5,.9]);sscale=rng.uniform(0,.3);mode=i%2
 put(pawn+0x374,'I',0);put(pawn+0x3f8,'fff',*v);put(pawn+0x35f0,'fff',*angles);put(pawn+0x530,'I',2 if grounded else 0xffffffff);put(pawn+0x1e80,'B',walking)
 put(vdata+0x748+mode*4,'f',speed);put(vdata+0x788+mode*4,'f',move);put(vdata+0x7b8,'ff',initial,apex);put(vdata+0x730,'i',1)
 put(weapon+0x1a00,'i',mode);put(weapon+0x1a18,'f',penalty);put(weapon+0x1a14,'f',turning)
 for obj,fmt,value in [(force,'f',forcespread),(no,'B',nospread),(air,'f',airscale),(jump,'f',impulse),(strafe,'B',strafing),(bias,'f',sbias),(scale,'f',sscale)]:put(obj+0x58,fmt,value)
 m=c.c_float(float('nan'));j=c.c_float(float('nan'));total=fn(weapon,c.byref(m),c.byref(j))
 params=[*v,*angles,speed,move,penalty,turning,initial,apex,airscale,impulse,forcespread,sbias,sscale,walking,int(grounded),int(nospread),int(strafing)]
 rows.append({'input':params,'native':[total,m.value if math.isfinite(m.value) else None,j.value if math.isfinite(j.value) else None]})
print('NATIVE_ACCURACY cases='+str(len(rows))+' completed',flush=True)
# Exercise the actual dirty-cache evaluator, with interpolation off and no move parent.
velocityfn=c.CFUNCTYPE(c.c_void_p,c.c_void_p,c.c_void_p)(base+0x219280)
velocity=[]
for raw,cached in [([250.,0.,300.],[0.,0.,0.]),([0.,0.,0.],[250.,0.,-500.]),([-40.,150.,-120.],[12.,7.,80.])]:
 put(pawn+0x374,'I',0x1000);put(pawn+0x430,'fff',*raw);put(pawn+0x3f8,'fff',*cached)
 out=(c.c_float*3)();velocityfn(pawn,c.byref(out));velocity.append({'raw':raw,'cached_before':cached,'native':list(out),'eflags_after':c.c_uint.from_address(pawn+0x374).value})
print('NATIVE_DIRTY_VELOCITY '+json.dumps(velocity),flush=True)
# Hold all weapon inputs fixed and vary only whether the native accessor refreshes velocity.
put(weapon+0x1a00,'i',0);put(weapon+0x1a18,'f',.02);put(weapon+0x1a14,'f',.01)
put(vdata+0x748,'f',250.);put(vdata+0x788,'f',.1);put(vdata+0x7b8,'ff',.4,.2)
put(pawn+0x530,'I',0xffffffff);put(pawn+0x1e80,'B',0)
for obj,fmt,value in [(force,'f',0.),(no,'B',0),(air,'f',1.),(jump,'f',300.),(strafe,'B',0)]:put(obj+0x58,fmt,value)
put(pawn+0x3f8,'fff',0.,0.,0.);put(pawn+0x430,'fff',250.,0.,300.)
put(pawn+0x374,'I',0);stale_total=fn(weapon,None,None)
put(pawn+0x374,'I',0x1000);refreshed_total=fn(weapon,None,None)
velocity_effect={'synthetic':True,'cache':[0.,0.,0.],'network_velocity':[250.,0.,300.],'stale_input_total':stale_total,'native_refreshed_total':refreshed_total,'delta':refreshed_total-stale_total}
print('NATIVE_VELOCITY_EFFECT '+json.dumps(velocity_effect),flush=True)

result={'client_sha256':hashlib.sha256(dll.read_bytes()).hexdigest(),'iat_bound':len(bound),'accuracy':rows,'velocity':velocity,'velocity_effect':velocity_effect,'scope':'private process fixture, no game writes, no executable code patches'}
(a/'native-accuracy.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
with (a/'native-accuracy.tsv').open('w',encoding='utf-8') as f:
 for row in rows:f.write(' '.join(str(v) for v in row['input']+[row['native'][0]])+'\n')
