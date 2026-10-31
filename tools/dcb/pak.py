import struct, sys, os, collections
def chunks(b):
    o=0; res=[]
    while o+8<=len(b):
        i,s,sz=struct.unpack_from('<hhi',b,o)
        if i<0: break
        res.append((i,s,o+8,sz)); o+=8+sz
    return res
if __name__=='__main__':
    for p in sys.argv[1:]:
        b=open(p,'rb').read(); print(p,len(b))
        for i,s,o,sz in chunks(b): print('  id',i,'sub',s,'off',o,'size',sz, b[o:o+8].hex())
