# The test screen: 3440x1440, 10 s at a given rate. Left half: ectonomic.com, still.
# Right half: KDE's code browser, still for 4 s, scrolling for 4 s, still for 2 s.
src_inputs() { echo "-loop 1 -framerate $1 -t 10 -i page-ectonomic.png -loop 1 -framerate $1 -t 10 -i page-code.png"; }
SRC_GRAPH="[1:v]crop=1720:1440:0:'if(lt(t,4),0,if(lt(t,8),floor((t-4)/4*2760),2760))'[r];[0:v][r]hstack=inputs=2,format=rgb24"
