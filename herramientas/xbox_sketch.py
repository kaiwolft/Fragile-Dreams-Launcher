import sys; sys.path.insert(0,'.')
import numpy as np, math, random
from PIL import Image, ImageDraw
from ink import Page
import cv2
from scipy.ndimage import binary_dilation, uniform_filter
random.seed(7)
q=Page('t197.png')
rgb=q.rgb
red=(rgb[:,:,0]>140)&(rgb[:,:,1]<130)&(rgb[:,:,2]<130)&(rgb[:,:,0]-rgb[:,:,1]>40)
m=np.zeros(red.shape,bool)
reg=np.zeros(red.shape,bool); reg[176:372,486:668]=True; reg[176:184,486:520]=False; reg[140:184,522:668]=True
m=reg&((q.alpha>0.03)|red)
# la sombra gris del dibujo: más oscuro que el papel local
lum=rgb@[0.299,0.587,0.114]; pl=q.paper@[0.299,0.587,0.114]
m|=reg&(pl-lum>3.5)
m=binary_dilation(m,iterations=3)&reg
img=rgb.clip(0,255).astype('uint8')[:,:,::-1].copy()
out=cv2.inpaint(img,(m*255).astype('uint8'),7,cv2.INPAINT_TELEA)[:,:,::-1].astype(float)
clean=rgb[4:26,300:700]; det=clean-np.stack([uniform_filter(clean[:,:,c],7) for c in range(3)],-1)
ys,xs=np.nonzero(m); out[ys,xs]+=det[ys%det.shape[0],xs%det.shape[1]]
rgb[m]=out[m]

# ---- dibujo del mando Xbox (vista frontal, ligeramente inclinado), a 4x
S=4; W,H=200,150; OX,OY=470,215            # zona en la página
cv=Image.new('L',(W*S,H*S),0); d=ImageDraw.Draw(cv)
def bez(p0,p1,p2,p3,n=40):
    return [((1-t)**3*p0[0]+3*(1-t)**2*t*p1[0]+3*(1-t)*t*t*p2[0]+t**3*p3[0],(1-t)**3*p0[1]+3*(1-t)**2*t*p1[1]+3*(1-t)*t*t*p2[1]+t**3*p3[1]) for t in [i/n for i in range(n+1)]]
def path(segs):
    pts=[]
    for sgm in segs: pts+=bez(*sgm)
    return pts
def wob(pts,a=0.35):
    return [(x+random.uniform(-a,a),y+random.uniform(-a,a)) for x,y in pts]
def line(pts,w=1.5,v=255):
    P=[(x*S,y*S) for x,y in pts]; d.line(P,fill=v,width=int(w*S),joint='curve')
def circ(cx,cy,r,w=1.3,v=255):
    pts=[(cx+r*math.cos(t/40*2*math.pi),cy+r*math.sin(t/40*2*math.pi)) for t in range(41)]
    line(wob(pts,0.2),w,v)
cx=100
# contorno del cuerpo (simétrico)
L=[((cx,30),(cx-25,29),(cx-52,28),(cx-66,34)),
   ((cx-66,34),(cx-84,40),(cx-92,62),(cx-96,90)),
   ((cx-96,90),(cx-100,118),(cx-94,132),(cx-82,132)),
   ((cx-82,132),(cx-70,132),(cx-62,112),(cx-50,100)),
   ((cx-50,100),(cx-38,92),(cx-20,92),(cx,92))]
left=path(L); right=[(2*cx-x,y) for x,y in left]
line(wob(left)); line(wob(right))
# gatillos (LT / RT) asomando arriba y bumpers
LT=path([((cx-70,33),(cx-74,18),(cx-60,10),(cx-46,12)),((cx-46,12),(cx-38,13),(cx-36,22),(cx-38,29))])
line(wob(LT),1.5); line(wob([(2*cx-x,y) for x,y in LT]),1.5)
LB=path([((cx-74,40),(cx-66,30),(cx-46,26),(cx-30,28))]); line(wob(LB),1.2); line(wob([(2*cx-x,y) for x,y in LB]),1.2)
# sticks, cruceta, botones
circ(cx-50,52,10); circ(cx-50,52,6,1.0)
circ(cx+26,74,10); circ(cx+26,74,6,1.0)
for dx,dy in ((0,-7),(0,7),(-7,0),(7,0)):
    pass
line([(cx-29,66),(cx-29,84)],3.0); line([(cx-38,75),(cx-20,75)],3.0)   # cruceta (se aclara después)
for bx,by in ((cx+50,40),(cx+50,60),(cx+40,50),(cx+60,50)): circ(bx,by,4.2,1.1)
circ(cx,40,6,1.1)     # botón central
for sx in (cx-12,cx+12): circ(sx,52,2.2,0.9)
# sombreado a rayitas en el lado izquierdo de los mangos
for i in range(12):
    y=96+i*2.6; line([(cx-92+i*0.6,y),(cx-84+i*0.4,y+5)],0.6,150)
ink=np.array(cv.resize((W,H),Image.LANCZOS)).astype(float)/255.0
ink=np.clip(ink*1.2,0,1)
INK=np.array([62,48,30],float)
sub=rgb[OY:OY+H,OX:OX+W]; rgb[OY:OY+H,OX:OX+W]=sub*(1-ink[...,None])+INK*ink[...,None]
# ---- círculo rojo alrededor de LT y línea hacia la etiqueta
cvr=Image.new('L',(W*S,H*S+200),0); dr=ImageDraw.Draw(cvr)
ltx,lty=OX+cx-55,OY+18
R=17
def rline(pts,w): dr.line([((x-OX)*S,(y-OY+50)*S) for x,y in pts],fill=255,width=int(w*S),joint='curve')
pts=[(ltx+R*math.cos(t/60*2*math.pi),lty+R*1.0*math.sin(t/60*2*math.pi)) for t in range(61)]
rline(pts,1.6)
rline([(512,179),(ltx-6,lty-R+1)],1.6)
redl=np.array(cvr.resize((W,H+50),Image.LANCZOS)).astype(float)/255.0
RED=np.array([196,52,48],float)
sub=rgb[OY-50:OY+H,OX:OX+W]; rgb[OY-50:OY+H,OX:OX+W]=sub*(1-redl[...,None])+RED*redl[...,None]
q.save('t197b.png')
Image.open('t197b.png').convert('RGB').crop((420,100,700,380)).resize((560,560)).save('sk197b.png')
