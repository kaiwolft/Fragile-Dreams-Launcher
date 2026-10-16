# Herramientas para editar el texto impreso de las páginas de tutorial conservando el papel y la tipografía
import numpy as np
from PIL import Image
from scipy.ndimage import median_filter
INK=np.array([80,56,10],float)
class Page:
    def __init__(s,path):
        s.img=Image.open(path).convert('RGBA'); a=np.array(s.img).astype(float)
        s.rgb=a[:,:,:3]; s.A=a[:,:,3]
        s.paper=np.stack([median_filter(s.rgb[:,:,c],size=9) for c in range(3)],-1)
        lum=lambda x: x@[0.299,0.587,0.114]
        pl=lum(s.paper); l=lum(s.rgb); il=lum(INK)
        s.alpha=np.clip((pl-l)/np.maximum(pl-il,20),0,1)
        s.alpha[s.alpha<0.12]=0
    def words(s,y0,y1,x0,x1,gap=3):
        col=s.alpha[y0:y1,x0:x1].max(axis=0)>0.25
        out=[];st=None;z=0
        for i,v in enumerate(col):
            if v:
                if st is None: st=i
                z=0; last=i
            elif st is not None:
                z+=1
                if z>=gap: out.append((x0+st,x0+last+1)); st=None
        if st is not None: out.append((x0+st,x0+last+1))
        return out
    def cut(s,y0,y1,xa,xb): return s.alpha[y0:y1,xa:xb].copy()
    def erase(s,y0,y1,xa,xb):
        # rellenar la tinta con el papel de alrededor (inpainting), no con una mediana que deja fantasmas
        import cv2
        from scipy.ndimage import binary_dilation
        m=np.zeros(s.alpha.shape,bool); m[y0:y1,xa:xb]=s.alpha[y0:y1,xa:xb]>0.04
        m=binary_dilation(m,iterations=2)
        img=s.rgb.clip(0,255).astype('uint8')[:,:,::-1].copy()
        out=cv2.inpaint(img,(m*255).astype('uint8'),5,cv2.INPAINT_TELEA)[:,:,::-1].astype(float)
        # grano del papel: el detalle fino de una franja limpia (arriba de la página) sobre la zona rellenada
        from scipy.ndimage import uniform_filter
        clean=s.rgb[4:26,300:700]; det=clean-np.stack([uniform_filter(clean[:,:,c],7) for c in range(3)],-1)
        ys,xs=np.nonzero(m); out[ys,xs]+=det[ys%det.shape[0],xs%det.shape[1]]
        s.rgb[m]=out[m]; s.alpha[m]=0
    def stamp(s,al,x,y,color=INK):
        h,w=al.shape; reg=s.rgb[y:y+h,x:x+w]
        al=np.clip(al*1.15,0,1)
        s.rgb[y:y+h,x:x+w]=reg*(1-al[...,None])+color*al[...,None]
    def save(s,path):
        out=np.dstack([s.rgb.clip(0,255),s.A]).astype('uint8'); Image.fromarray(out,'RGBA').save(path)
