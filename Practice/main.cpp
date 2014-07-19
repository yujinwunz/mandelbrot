#include <iostream>
#include <windows.h>
#include <process.h>
#include <queue>
#include <utility>
#include "SDL.h"
#include <mpir.h>
#include <mpirxx.h>
using namespace std;
#undef main
//constants
const int CALC = 1;

//global variables
int grid[3000][2000];	//for the screen
int screenH, screenW;
mpf_class lef, top, step;
mpq_class qzoom(1);
int maxIter = 360;
int saved= 0, processed = 0;
int pixel = 1;
int prec = 60;

double dense = 100.0;

SDL_Surface *screen;

int workers = 0, bosses = 0;	//threading
HANDLE workM, bossM;
pair<int,int> proc[2000000];
int front=0, back = 0;
const int MAX_THREADS = 8;
HANDLE procM[MAX_THREADS+1];

bool running;
//end global variables
int makeCol(int iters){
	if(iters==0) return 0;
	int g = abs(((int)(sqrt((float)iters)*dense)&511)-255);
	int b = abs(((int)(sqrt((float)iters)*dense*1.5)&511)-255);
	int r = abs(((int)(sqrt((float)iters)*dense*5.6)&511)-255);
	return (r<<16)|(g<<8)|(b<<0);
}

void display(int x, int y, int size, int col){
	//displays a pixel on screen.
	if(size==0) size = pixel;
	SDL_Rect r = {x*size,y*size,size,size};
	SDL_FillRect(screen,&r,makeCol(col));
}

void mandelbrot( int x, int y){
	//draws pixel on grid.
	mpf_class x0(lef + x*step,prec);
	mpf_class y0(top + y*step,prec);
	mpf_class xx(0,prec), yy(0,prec), tempx(0,prec);
	int iters = 0;

	while(iters < maxIter && xx*xx+yy*yy<4.0){
		iters++;
		tempx = xx*xx-yy*yy+x0;
		yy = 2*xx*yy+y0;
		xx = tempx;
	}

	if(iters==maxIter) grid[x][y] = 0;
	else grid[x][y] = iters;
	WaitForSingleObject(bossM,INFINITE);
	processed ++;
	ReleaseMutex(bossM);
	display(x,y,0,grid[x][y]);
	//SDL_Flip(screen);
}

void drawStrip(int x, int y, int xw, int yh){
	for(int i = x; i <= xw; i++){
		for(int j = y; j <= yh; j++){
			if(grid[i][j] == -1) mandelbrot(i,j);
		}
	}
}

int checkStrip( int x, int y, int xw, int yh){
	//returns hori-2 if the strip is not single colour. returns colour if it is.
	int col = -3;
	bool ret = true;
	for(int i = x; i <= xw; i++){
		for(int j = y; j <= yh; j++){
			//if(grid[i][j] == -1) mandelbrot(i,j);
			if(col==-3) col = grid[i][j];
			if(col != grid[i][j]) ret = false;
		}
	}
	if(ret){
		return col;
	}
	return -abs(xw+yh+x+y);
}

void drawBox( int x, int y, int xw, int yh){
	//draws box using recursion.
	//base cases. box check.
	int a = checkStrip(x,y,xw,y);
	int b = checkStrip(x,y+1,x,yh-1);
	int c = checkStrip(x,yh,xw,yh);
	int d = checkStrip(xw,y+1,xw,yh-1);

	if(a==b&&b==c&&c==d){
		//draw box
		WaitForSingleObject(bossM,INFINITE);
		for(int i = x+1; i < xw; i++){
			for(int j = y+1; j < yh; j++){
				grid[i][j] = a;
				display(i,j,0,grid[i][j]);
				//SDL_Flip(screen);
				//record as saved
				processed++;
			}
		}
		
		saved += (xw-x-2)*(yh-y-2);
		ReleaseMutex(bossM);
		return;
	}

	if(yh-y<=1 || xw-x<=1){
		//box too small to care about anymore, now that borders are drawn.
		return;
	}

	//the centre is filled with uncalculated chaos. Calculate it.
	if(xw-x>yh-y){
		//split lef/right
		drawStrip((x+xw)/2,y,(x+xw)/2,yh);
		WaitForSingleObject(workM,INFINITE);
		proc[back++] = (make_pair((((x+xw)/2)<<12)|y,((xw)<<12)|yh));
		if(back==2000000) back = 0;
		proc[back++] = make_pair((x<<12)|y,(((x+xw)/2)<<12)|yh);
		if(back==2000000) back = 0;
		ReleaseMutex(workM);
	}else{
		//split top/bottom
		drawStrip(x,(y+yh)/2,xw,(y+yh)/2);
		WaitForSingleObject(workM,INFINITE);
		proc[back++] = make_pair((x<<12)|y,(xw<<12)|((y+yh)/2));
		if(back==2000000) back = 0;
		proc[back++] = make_pair((x<<12)|((y+yh)/2),(xw<<12)|yh);
		if(back==2000000) back = 0;
		ReleaseMutex(workM);
	}
}

//
void process(void*Param){
	//loops and processes whatever it can find on the queue, then exits when frame is complete.
	int s, f;
	WaitForSingleObject(procM[((int)Param)&31],INFINITE);
	while(processed<(((int)Param)>>5)){
		WaitForSingleObject(workM,INFINITE);
		if(front!=back){
			s = proc[front].first;
			f = proc[front].second;
			front ++;
			if(front==2000000) front = 0;
			ReleaseMutex(workM);

			drawBox(s>>12,s&4095,f>>12,f&4095);
		}else{
			ReleaseMutex(workM);
		}
	}
	ReleaseMutex(procM[((int)Param)&31]);
}
//here, double will be replaced with mpf equivalents.
void stopRender(){
	WaitForSingleObject(workM,INFINITE);
	WaitForSingleObject(bossM,INFINITE);
	processed = 500000000;
	ReleaseMutex(workM);
	ReleaseMutex(bossM);

	for(int i = 0; i <= MAX_THREADS; i++){
		WaitForSingleObject(procM[i],INFINITE);
		ReleaseMutex(procM[i]);
	}
}

void render(void*Param){
	stopRender();
	//clear all threads
	if(((int)Param)&CALC){
		WaitForSingleObject(workM,INFINITE);
		WaitForSingleObject(bossM,INFINITE);
		saved = processed = 0;
		front = back;
		ReleaseMutex(workM);
		ReleaseMutex(bossM);

		for(int i = 1; i <= MAX_THREADS; i++){
			WaitForSingleObject(procM[i],INFINITE);
			ReleaseMutex(procM[i]);
		}
	}
	WaitForSingleObject(procM[0],INFINITE);

	
	mpf_class clef, cTop;
	clef.set_prec(prec); cTop.set_prec(prec);
	int cH = screenH/pixel+1, cW = screenW/pixel+1;
	step *= pixel;	//zoom issues.

	
	//trippy recursion.
	SDL_FillRect(screen,NULL,(255<<16)|(255<<8)|(255));
	if(((int)Param)&CALC){
		for(int i = 0; i < cW; i++){
			for(int j = 0; j < cH; j++){
				grid[i][j] = -1;
			}
		}
		//prepare box
		drawStrip(0,0,cW-1,0);
		drawStrip(0,0,0,cH-1);
		drawStrip(cW-1,0,cW-1,cH-1);
		drawStrip(0,cH-1,cW-1,cH-1);

		WaitForSingleObject(workM,INFINITE);
		proc[back++] = make_pair(0,((cW-1)<<12)|(cH-1));
		if(back==2000000) back = 0;
		ReleaseMutex(workM);

		HANDLE thread[MAX_THREADS];
		for(int i = 0; i < MAX_THREADS; i++){
			thread[i] = (HANDLE)_beginthread(process,0,(void*)(((cH*cW)<<5)|(i+1)));
		}
	}
	

	for(int i = 0; i < screenW; i++){
	//	for(int j = 0; j < screenH; j++) mandelbrot(i,j);
	}
	//wait for rendering to finish
	while(processed < cH*cW){
		if(((int)Param)&CALC){}
		else processed = cH*cW;
		Sleep(10);
		SDL_Flip(screen);
	}
	
	cout<<processed<<" "<<cH*cW<<"\n";
//draw
	for(int i = 0; i < cW; i++){
		for(int j = 0; j < cH; j++){
			display(i,j,pixel,grid[i][j]);
		}
	}
	SDL_Flip(screen);
	cout<<"Saved "<<(float)((double)saved/(double)(screenH*screenW)*100.0)<<"% of calculations, "<<saved<<"\n";
	cout<<"Zoom: 2^"<<qzoom.get_str(10)<<"x. Iterations: "<<maxIter<<". Bits of precision: "<<prec<<".\n";
	cout<<"Iterations: "<<maxIter<<"\n";
	ReleaseMutex(procM[0]);
}

//prereq
int init(){
	if(SDL_Init(SDL_INIT_EVERYTHING) != 0) return -1;
	screen = SDL_SetVideoMode(600,400,32,SDL_DOUBLEBUF|SDL_RESIZABLE);

}

void setPrec(){
	top.set_prec(prec);
	lef.set_prec(prec);
	step.set_prec(prec);
}

//event handlers
void handle(SDL_Event e){
	if(e.type == SDL_QUIT) running = false;
	if(e.type == SDL_MOUSEBUTTONDOWN){
		//Zoom in or out
		int zoom ;
		if(e.button.button == SDL_BUTTON_MIDDLE) return;
		if(e.button.button == SDL_BUTTON_LEFT) zoom = 16;
		if(e.button.button == SDL_BUTTON_RIGHT) zoom = 1;

		lef = lef+(double)e.button.x*step-(double)screenW*step/(double)zoom/1.0;
		top = top+(double)e.button.y*step-(double)screenH*step/(double)zoom/1.0;

		step = step/(double)(zoom/2.0);
		_beginthread(render,0,(void*)CALC);
		if(zoom==16) qzoom += 3;
		else qzoom -= 1;
	}else if(e.type == SDL_KEYDOWN){
		//key presses.
		if(e.key.keysym.sym == SDLK_UP){
			//iterations!
			maxIter*=2;
			_beginthread(render,0,(void*)CALC);
		}else if(e.key.keysym.sym == SDLK_DOWN){
			maxIter = max(maxIter/2,1);
			_beginthread(render,0,(void*)CALC);
		}else if(e.key.keysym.sym == SDLK_LEFT){
			//increase precision
			prec*=2;
			setPrec();
			_beginthread(render,0,(void*)CALC);
		}else if(e.key.keysym.sym == SDLK_RIGHT){
			//decrease 
			prec/=2;
			setPrec();
			_beginthread(render,0,(void*)CALC);
		}else if(e.key.keysym.sym == SDLK_PAGEUP){
			//increase colour density
			dense *= 2;
			_beginthread(render,0,(void*)NULL);
		}else if(e.key.keysym.sym == SDLK_PAGEDOWN){
			//decrease colour density
			dense /= 2;
			_beginthread(render,0,(void*)NULL);
		}
	}else if(e.type == SDL_VIDEORESIZE){
		screenW = e.resize.w;
		screenH = e.resize.h;
		SDL_SetVideoMode(e.resize.w,e.resize.h,32,SDL_DOUBLEBUF|SDL_RESIZABLE);
		_beginthread(render,0,(void*)CALC);
	}

}

int main(){
	workM = CreateMutex(NULL,false,"Worker Guard");
	bossM = CreateMutex(NULL,false,"Boss Guard");
	for(int i = 0; i <= MAX_THREADS; i++) procM[i] = CreateMutex(NULL,false,"Process mutex #"+char(i));

	init();

	screenH = 400; screenW = 600;
	lef = -2; top = -1;
	step = 3.0/(double)screenW;

	running = true;
	//enter event state.
	_beginthread(render,0,(void*)CALC);
	SDL_Event e;
	while(running){
		//drawing or not drawing
		SDL_WaitEvent(&e);
		do{
			handle(e);
		}while(SDL_PollEvent(&e));
	}
	return 0;
}