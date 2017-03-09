LOAD DATA
INFILE './data/POLYHEDRAS.dat'
BADFILE './data/POLYHEDRAS.bad'
DISCARDFILE './data/POLYHEDRAS.dsc'
INSERT INTO TABLE CMSINTEGRATION.POLYHEDRA
FIELDS TERMINATED by ","
(
 DELTAPHI INTEGER EXTERNAL,  
 SOL_NAME CHAR,
 NUMSIDE INTEGER EXTERNAL,  
 STARTPHI INTEGER EXTERNAL,  
 RZ_ZS CHAR    
)