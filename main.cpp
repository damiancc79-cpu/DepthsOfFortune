#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

constexpr int MW=41, MH=41, VW=76, VH=30;
constexpr double PI=3.141592653589793, FOV=PI/3.0;

struct Weapon { string name; int damage, range, price; char glyph; };
const array<Weapon,5> weapons {{
  {"Rusty Blade",  10, 2,  0, '/'},
  {"Iron Cleaver", 18, 3, 18, '}'},
  {"Ember Wand",   27, 6, 38, '*'},
  {"Void Repeater",38, 9, 70, '='},
  {"Crown Cannon", 55,12,120, '@'}
}};
struct Entity { double x,y; int hp; char kind; bool alive=true; int stock=1; };
struct Room { int x,y,w,h; };

static termios oldTerm;
static bool terminalReady=false;
void restoreTerminal(){ if(terminalReady){ tcsetattr(STDIN_FILENO,TCSANOW,&oldTerm); cout<<"\x1b[?25h\x1b[0m\n"; terminalReady=false; } }
void rawTerminal(){
  tcgetattr(STDIN_FILENO,&oldTerm); termios t=oldTerm;
  t.c_lflag &= ~(ICANON|ECHO); t.c_cc[VMIN]=0; t.c_cc[VTIME]=0;
  tcsetattr(STDIN_FILENO,TCSANOW,&t); terminalReady=true;
  atexit(restoreTerminal); signal(SIGINT,[](int){ restoreTerminal(); _exit(0); });
  cout<<"\x1b[2J\x1b[?25l";
}
int key(){
  unsigned char c; if(read(STDIN_FILENO,&c,1)!=1) return 0;
  if(c==27){ unsigned char a,b; if(read(0,&a,1)==1 && read(0,&b,1)==1 && a=='[') return 1000+b; }
  return c;
}

class Game {
  vector<string> map;
  vector<Entity> ents;
  vector<Room> rooms;
  mt19937 rng{random_device{}()};
  double px=2.5,py=2.5,pa=0;
  int hp=100, gold=8, weapon=0, floorNo=1, kills=0;
  string message="Find the stairs. Fortune favors the armed.";
  bool running=true, won=false;
  int tick=0;

  int rnd(int a,int b){ uniform_int_distribution<int>d(a,b); return d(rng); }
  bool wall(double x,double y) const {
    int ix=(int)x,iy=(int)y; return ix<0||iy<0||ix>=MW||iy>=MH||map[iy][ix]=='#';
  }
  bool occupied(int x,int y,const Entity* ignore=nullptr) const {
    for(auto&e:ents) if(&e!=ignore&&e.alive&&(int)e.x==x&&(int)e.y==y&&e.kind!='c') return true;
    return false;
  }
  void corridor(int x1,int y1,int x2,int y2){
    if(rnd(0,1)){ for(int x=min(x1,x2);x<=max(x1,x2);x++)map[y1][x]='.';
      for(int y=min(y1,y2);y<=max(y1,y2);y++)map[y][x2]='.';
    } else { for(int y=min(y1,y2);y<=max(y1,y2);y++)map[y][x1]='.';
      for(int x=min(x1,x2);x<=max(x1,x2);x++)map[y2][x]='.'; }
  }
  pair<int,int> freeSpot(int roomIndex){
    Room&r=rooms[roomIndex];
    for(int tries=0;tries<100;tries++){ int x=rnd(r.x+1,r.x+r.w-2),y=rnd(r.y+1,r.y+r.h-2);
      if(!occupied(x,y) && ((int)px!=x||(int)py!=y)) return {x,y}; }
    return {r.x+1,r.y+1};
  }
  void generate(){
    map.assign(MH,string(MW,'#')); ents.clear(); rooms.clear();
    for(int attempts=0;attempts<220 && rooms.size()<11;attempts++){
      Room r{rnd(1,MW-10),rnd(1,MH-9),rnd(5,9),rnd(5,8)}; bool ok=true;
      for(auto&o:rooms) if(r.x<=o.x+o.w+1&&r.x+r.w+1>=o.x&&r.y<=o.y+o.h+1&&r.y+r.h+1>=o.y)ok=false;
      if(!ok)continue;
      for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)map[y][x]='.';
      if(!rooms.empty()) corridor(rooms.back().x+rooms.back().w/2,rooms.back().y+rooms.back().h/2,
                                  r.x+r.w/2,r.y+r.h/2);
      rooms.push_back(r);
    }
    if(rooms.size()<4){ message="The dungeon resisted creation. Try again."; return; }
    px=rooms[0].x+rooms[0].w/2+.5; py=rooms[0].y+rooms[0].h/2+.5; pa=0;
    auto stairs=freeSpot((int)rooms.size()-1); map[stairs.second][stairs.first]='>';
    int enemyCount=7+floorNo*3;
    for(int i=0;i<enemyCount;i++){ auto p=freeSpot(rnd(1,(int)rooms.size()-1));
      ents.push_back({p.first+.5,p.second+.5,20+floorNo*8,'e'}); }
    int shops=max(2,(int)rooms.size()/4);
    for(int i=0;i<shops;i++){ auto p=freeSpot(rnd(1,(int)rooms.size()-1));
      ents.push_back({p.first+.5,p.second+.5,999,'s',true,min(4,max(1,weapon+1+rnd(0,1)))}); }
    for(int i=0;i<5;i++){ auto p=freeSpot(rnd(1,(int)rooms.size()-1));
      ents.push_back({p.first+.5,p.second+.5,1,'c'}); }
  }
  bool clearLine(double x,double y) const {
    double dx=x-px,dy=y-py, dist=hypot(dx,dy);
    for(double d=.2;d<dist;d+=.15) if(wall(px+dx/dist*d,py+dy/dist*d))return false;
    return true;
  }
  void attack(){
    const Weapon&w=weapons[weapon]; Entity*best=nullptr; double bd=1e9;
    for(auto&e:ents) if(e.alive&&e.kind=='e'){
      double dx=e.x-px,dy=e.y-py,d=hypot(dx,dy);
      double diff=atan2(sin(atan2(dy,dx)-pa),cos(atan2(dy,dx)-pa));
      if(d<=w.range && abs(diff)<.12+0.12/d && clearLine(e.x,e.y) && d<bd){best=&e;bd=d;}
    }
    if(!best){ message="Your "+w.name+" cuts only shadow."; return; }
    int dmg=w.damage+rnd(-2,4); best->hp-=dmg;
    if(best->hp<=0){ best->alive=false; int loot=rnd(3,8)+floorNo; gold+=loot;kills++;
      message="Enemy slain! +"+to_string(loot)+" gold."; if(rnd(0,3)==0) ents.push_back({best->x,best->y,1,'c'}); }
    else message="Hit for "+to_string(dmg)+". It has "+to_string(best->hp)+" HP.";
    enemiesMove();
  }
  void interact(){
    for(auto&e:ents)if(e.alive&&e.kind=='s'&&hypot(e.x-px,e.y-py)<1.8){
      int s=e.stock; if(s<=weapon){message="This merchant has nothing finer.";return;}
      if(gold<weapons[s].price){message="Need "+to_string(weapons[s].price)+" gold for "+weapons[s].name+".";return;}
      gold-=weapons[s].price;weapon=s;e.alive=false;message="Bought "+weapons[s].name+"!";return;
    }
    message="No merchant close enough.";
  }
  void enemiesMove(){
    tick++;
    for(auto&e:ents)if(e.alive&&e.kind=='e'){
      double d=hypot(e.x-px,e.y-py);
      if(d<1.25){int dmg=rnd(3,6)+floorNo; hp-=dmg; message+="  Enemy hits for "+to_string(dmg)+"!";continue;}
      if(d<8&&clearLine(e.x,e.y)){
        double nx=e.x+(px-e.x)/d*.28, ny=e.y+(py-e.y)/d*.28;
        if(!wall(nx,ny)&&!occupied((int)nx,(int)ny,&e)){e.x=nx;e.y=ny;}
      } else if(tick%3==0){ double a=rnd(0,628)/100.; double nx=e.x+cos(a)*.2,ny=e.y+sin(a)*.2;
        if(!wall(nx,ny))e.x=nx,e.y=ny; }
    }
    if(hp<=0) running=false;
  }
  void move(double amount){
    double nx=px+cos(pa)*amount,ny=py+sin(pa)*amount;
    if(!wall(nx,py))px=nx;if(!wall(px,ny))py=ny;
    for(auto&e:ents)if(e.alive&&e.kind=='c'&&hypot(e.x-px,e.y-py)<.65){
      int v=rnd(2,6);gold+=v;e.alive=false;message="Picked up "+to_string(v)+" gold.";}
    if(map[(int)py][(int)px]=='>'){
      floorNo++; if(floorNo>5){won=true;running=false;return;} hp=min(100,hp+25);generate();
      message="Floor "+to_string(floorNo)+". The walls grow hungry.";
    }
    enemiesMove();
  }
  char texture(double hitX,double hitY,double dist) const {
    double f=fabs(hitX-round(hitX))<.02?hitY:hitX; int brick=(int)(f*4)&1;
    if(dist<3)return brick?'#':'%'; if(dist<7)return brick?'O':'o'; if(dist<13)return ':'; return '.';
  }
  void render(){
    vector<string> out(VH,string(VW,' ')); vector<double> depth(VW,99);
    for(int x=0;x<VW;x++){
      double ra=pa-FOV/2+FOV*x/VW,dist=0,hx=px,hy=py;
      while(dist<20){dist+=.035;hx=px+cos(ra)*dist;hy=py+sin(ra)*dist;if(wall(hx,hy))break;}
      dist*=cos(ra-pa);depth[x]=dist;int ceiling=max(0,(int)(VH/2-VH/dist*.72)),floor=min(VH-1,VH-ceiling);
      for(int y=0;y<VH;y++){
        if(y<ceiling) out[y][x]=(y>VH/2-3?'.':' ');
        else if(y<=floor)out[y][x]=texture(hx,hy,dist);
        else {double shade=(double)(y-VH/2)/(VH/2);out[y][x]=shade>.75?'#':shade>.45?':':'.';}
      }
    }
    vector<pair<double,Entity*>> visible;
    for(auto&e:ents)if(e.alive){double dx=e.x-px,dy=e.y-py,d=hypot(dx,dy);
      double a=atan2(sin(atan2(dy,dx)-pa),cos(atan2(dy,dx)-pa));
      if(abs(a)<FOV*.65&&clearLine(e.x,e.y))visible.push_back({d,&e});}
    sort(visible.rbegin(),visible.rend(),[](auto&a,auto&b){return a.first<b.first;});
    for(auto [d,e]:visible){
      double a=atan2(sin(atan2(e->y-py,e->x-px)-pa),cos(atan2(e->y-py,e->x-px)-pa));
      int sx=(int)((a+FOV/2)/FOV*VW),size=max(1,(int)(VH/(d*1.25))),cy=VH/2;
      char g=e->kind=='e'?'M':e->kind=='s'?'$':'*';
      for(int yy=cy-size/2;yy<=cy+size/2;yy++)for(int xx=sx-size/3;xx<=sx+size/3;xx++)
        if(xx>=0&&xx<VW&&yy>=0&&yy<VH&&d<depth[xx])out[yy][xx]=g;
    }
    string color="\x1b[38;5;220m";
    cout<<"\x1b[H\x1b[38;5;214m DEPTHS OF FORTUNE \x1b[0m  Floor "<<floorNo<<"/5  HP ";
    cout<<(hp<30?"\x1b[31m":"\x1b[32m")<<hp<<"\x1b[0m  Gold \x1b[33m"<<gold<<"\x1b[0m  "
        <<weapons[weapon].name<<" ["<<weapons[weapon].damage<<" dmg]\n";
    for(int y=0;y<VH;y++){
      cout<<"\x1b[38;5;"<<(y<VH/2?"244m":"238m")<<out[y]<<"\x1b[0m ";
      if(y<20){
        for(int mx=max(0,(int)px-10);mx<=min(MW-1,(int)px+10);mx++){
          int my=max(0,(int)py-10)+y;if(my>=MH){cout<<' ';continue;}
          char c=map[my][mx]=='#'?'#':map[my][mx]=='>'?'>':' ';
          for(auto&e:ents)if(e.alive&&(int)e.x==mx&&(int)e.y==my)c=e.kind=='e'?'m':e.kind=='s'?'$':'*';
          if((int)px==mx&&(int)py==my)c='@';cout<<c;
        }
      }
      cout<<"\n";
    }
    cout<<"\x1b[38;5;245m WASD move/turn  arrows turn  SPACE attack  E buy  R new game  Q quit\x1b[0m\n";
    cout<<"\x1b[38;5;229m "<<message<<string(max(0,105-(int)message.size()),' ')<<"\x1b[0m";
    cout.flush();
  }
 public:
  Game(){generate();}
  void reset(){hp=100;gold=8;weapon=0;floorNo=1;kills=0;won=false;running=true;generate();message="A new descent begins.";}
  void loop(){
    rawTerminal();
    render();
    while(true){
      int k=key();
      if(!k){this_thread::sleep_for(chrono::milliseconds(10));continue;}
      if(k=='q'||k=='Q')break;
      if(!running){if(k=='r'||k=='R')reset();else {this_thread::sleep_for(chrono::milliseconds(30));continue;}}
      else if(k=='w'||k=='W')move(.32); else if(k=='s'||k=='S')move(-.25);
      else if(k=='a'||k=='A'||k==1000+'D')pa-=.15;
      else if(k=='d'||k=='D'||k==1000+'C')pa+=.15;
      else if(k==' ')attack(); else if(k=='e'||k=='E')interact();
      if(!running){message=won?"YOU ESCAPED RICH! Kills: "+to_string(kills)+". Press R to delve again."
                               :"YOU DIED. Gold is cold comfort. Press R to try again.";}
      render();
    }
  }
};

int main(){
  if(!isatty(STDIN_FILENO)){cerr<<"Run this game in a terminal.\n";return 1;}
  Game game;game.loop();return 0;
}
