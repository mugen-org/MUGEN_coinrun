/*
Source code for the CoinRun environment.

This is built as a shared library and loaded into python with ctypes.
It exposes a C interface similar to that of a VecEnv.

Also includes a mode that creates a window you can interact with using the keyboard.

Copyright (c) Meta Platforms, Inc. All Right reserved.
*/

#include <QtCore/QMutexLocker>
#include <QtCore/QWaitCondition>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QPushButton>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QToolButton>
#include <QtCore/QDir>
#include <QtCore/QThread>
#include <QtCore/QProcess>
#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDirIterator>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <cmath>
#include <math.h>
#include <random>
#include <iostream>
#include <sstream>
#include <memory>
#include <assert.h>
#include <set>
#include <map>

const int NUM_ACTIONS = 7;
const int MAZE_OFFSET = 1;

static
int DISCRETE_ACTIONS[NUM_ACTIONS * 2] = {
    0, 0,
    +1, 0,  // right
    -1, 0,  // left
    0, +1,  // jump
    +1, +1, // right-jump
    -1, +1, // left-jump
    0, -1,  // down  (step down from a crate)
};

#define VIDEORES 1024
#define VIDEORES_STR "1024"

const std::map<std::string, int> AUDIO_LABEL_MAP = {
    { "ladder_climbing", 0 },
    { "jump", 1 },
    { "walk", 2 },
    { "bumped_head", 3 },
    { "killed", 4 },
    { "coin", 5 },
    { "killed_monster", 6 },
    { "gem", 7 },
    { "power_up_mode", 8 },
};

const char SPACE = '.';
const char LADDER = '=';
const char LAVA_SURFACE = '^';
const char LAVA_MIDDLE = '|';
const char WALL_SURFACE = 'S';
const char WALL_MIDDLE = 'A';
const char COIN_OBJ1 = '1';
const char COIN_OBJ2 = '2';
const char SPIKE_OBJ = 'P';
const char FLYING_MONSTER = 'F';
const char WALKING_MONSTER = 'M';
const char GROUND_MONSTER = 'G';

const int DOWNSAMPLE = 16;
const float LADDER_MIXRATE_Y = 0.4;
const float LADDER_MIXRATE_X = 0.1;
const float LADDER_V = 0.4;
const float MONSTER_SPEED = 0.05;
const float MONSTER_MIXRATE = 0.05;
const float GRAVITY = 0.08;
const float MAX_JUMP = 0.9;
const float MAX_SPEED = 0.2;
const float MIX_RATE = 0.1;
float AIR_CONTROL = 0.15;

const int RES_W = 64;
const int RES_H = 64;

const int AUDIO_MAP_SIZE = 9;

const int DEATH_ANIM_LENGTH = 30;
const int FINISHED_LEVEL_ANIM_LENGTH = 20;
const int MONSTER_DEATH_ANIM_LENGTH = 2;

float BUMP_HEAD_PENALTY = 0.0;
float DIE_PENALTY = 0.0;
float KILL_MONSTER_REWARD = 5.0;
float JUMP_PENALTY = 0.0;
float SQUAT_PENALTY = 0.0;
float JITTER_SQUAT_PENALTY = 0.0;

bool USE_LEVEL_SET = false;
int NUM_LEVELS = 0;
int *LEVEL_SEEDS;
int LEVEL_TIMEOUT = 1000;

bool RANDOM_TILE_COLORS = false;
bool PAINT_VEL_INFO = false;
bool USE_DATA_AUGMENTATION = false;

static bool shutdown_flag = false;
static std::string monitor_dir;
static int monitor_csv_policy;

const char* test =
"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
"A.....................F................A"
"A.................F....................A"
"A.............F........................A"
"AAA..................................AAA"
"AA....................................AA"
"AAA..................................AAA"
"AA....................................AA"
"AAA..................................AAA"
"A.......F..............................A"
"A................F.....................A"
"A.........................F............A"
"A......................................A"
"A......................................A"
"A......................................A"
"A......................................A"
"A......................................A"
"A...................G.......G..........A"
"A.................aSSS^^^^^SSSb........A"
"A....................AAAAAAA...........A"
"A......................................A"
"A...................................F..A"
"A......................................A"
"A........1.1.1M1.1.1.........=...1.....A"
"A......aSSSSSSSSSSSSSb....aSb=..aSb....A"
"A............................=.........A"
"A............................=.........A"
"A....  ......................=.........A"
"A... .. .....=...2.2.2.2.2...=.........A"
"A. ..... ....=aSSSSSSSSSSSSSSb.........A"
"A............=.........................A"
"A............=.........................A"
"A..=.........=...............F.........A"
"A..=...................................A"
"A..=.......#&..........................A"
"A..........$%..........................A"
"A.....#$.#%$#...........S^^^^^^S.......A"
"A.....%#.$&%#.....M..M..A||||||A.......A"
"ASSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS^^A"
"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
;

static std::vector<int> ground_theme_idxs;
static std::vector<int> walking_theme_idxs;
static std::vector<int> flying_theme_idxs;

bool is_crat(char c) {
  return c=='#' || c=='$' || c=='&' || c=='%';
}

bool is_wall(char c, bool crate_counts=false)
{
  bool wall = c=='S' || c=='A' || c=='a' || c=='b' || c=='a';
  if (crate_counts)
    wall |= is_crat(c);
  return wall;
}
bool is_lethal(char c) {
  return c == LAVA_SURFACE || c == LAVA_MIDDLE || c == SPIKE_OBJ;
}
bool is_coin(char c) {
  return c==COIN_OBJ1;
}
bool is_gem(char c) {
  return c==COIN_OBJ2;
}

class VectorOfStates;

class RandGen {
public:
  bool is_seeded = false;
  std::mt19937 stdgen;

  int randint(int low, int high) {
    assert(is_seeded);
    uint32_t x = stdgen();
    uint32_t range = high - low;
    return low + (x % range);
  }

  float rand01() {
    assert(is_seeded);
    uint32_t x = stdgen();
    return (double)(x) / ((double)(stdgen.max()) + 1);
  }

  int randint() {
    assert(is_seeded);
    return stdgen();
  }

  void seed(int seed) {
    stdgen.seed(seed);
    is_seeded = true;
  }
};

static RandGen global_rand_gen;

double get_time() {
  struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time); // you need macOS Sierra 10.12 for this
  return time.tv_sec + 0.000000001 * time.tv_nsec;
}

std::string stdprintf(const char *fmt, ...)
{
  char buf[32768];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  buf[32768 - 1] = 0;
  return buf;
}

inline double sqr(double x)  { return x*x; }
inline double sign(double x)  { return x > 0 ? +1 : (x==0 ? 0 : -1); }
inline int min(int a, int b) { return a < b ? a : b; }
inline int max(int a, int b) { return a > b ? a : b; }
inline float clip_abs(float x, float y)  { if (x > y) return y; if (x < -y) return -y; return x; }

class Maze;

const int MONSTER_TRAIL = 14;

class Monster {
public:
  float x, y;
  float prev_x[MONSTER_TRAIL], prev_y[MONSTER_TRAIL];
  float vx = 0.01, vy = 0;
  bool is_flying;
  bool is_walking;
  bool is_dead = false;
  int monster_dying_frame_cnt = 0;
  int theme_n = -1;
  int pause = 0;
  void step(const std::shared_ptr<Maze>& maze);
};

class Maze {
public:
  int spawnpos[2];
  int w, h;
  int* walls;
  int coins;
  bool is_terminated;
  bool is_new_level = true;

  float gravity;
  float max_jump;
  float air_control;
  float max_dy;
  float max_dx;
  float default_zoom;
  float max_speed;
  float mix_rate;

  std::vector<std::shared_ptr<Monster>> monsters;

  Maze(const int _w, const int _h)
  {
    w = _w;
    h = _h;
    walls = new int[w*h];
    is_terminated = false;
    coins = 0;
  }

  ~Maze()
  {
    delete[] walls;
  }

  int& get_elem(int x, int y)
  {
    return walls[w*y + x];
  }

  int set_elem(int x, int y, int val)
  {
    return walls[w*y + x] = val;
  }

  void fill_elem(int x, int y, int dx, int dy, char elem)
  {
    for (int j = 0; j < dx; j++) {
      for (int k = 0; k < dy; k++) {
        set_elem(x + j, y + k, elem);
      }
    }
  }

  bool has_vertical_space(float x, float y, bool crate_counts)
  {
    return !(
      is_wall(get_elem(x + .1, y)) || is_wall(get_elem(x + .9, y))
      || (crate_counts && is_crat(get_elem(x + .1, y)))
      || (crate_counts && is_crat(get_elem(x + .9, y)))
      );
  }

  void init_physics() {
    default_zoom = 5.5;
      
    gravity = GRAVITY;
    air_control = AIR_CONTROL;
    
    max_jump = MAX_JUMP;
    max_speed = MAX_SPEED;
    mix_rate = MIX_RATE;

    max_dy = max_jump * max_jump / (2*gravity);
    max_dx = max_speed * 2 * max_jump / gravity;
  }
};

class RandomMazeGenerator {
public:
  struct Rec {
    int x;
    int y;
  };

  struct Wall {
      int x1;
      int y1;
      int x2;
      int y2;
  };

  std::vector<Rec> rec_stack;
  std::shared_ptr<Maze> maze;
  RandGen rand_gen;

  int dif;
  int maze_dim;
  int danger_type;

  std::set<int> cell_sets[2000];
  int free_cells[2000];
  int num_free_cells;

  std::set<int> lookup(std::set<int> *cell_sets, int x, int y) {
      return cell_sets[maze_dim*y + x];
  }

  bool is_maze_wall(int x, int y) {
      return is_wall(maze->get_elem(x + MAZE_OFFSET, y + MAZE_OFFSET));
  }

  void set_free_cell(int x, int y) {
      maze->set_elem(x + MAZE_OFFSET, y + MAZE_OFFSET, SPACE);
      free_cells[num_free_cells] = maze_dim*y + x;
      num_free_cells += 1;
  }

  void fill_block_top(int x, int y, int dx, int dy, char fill, char top)
  {
    assert(dy > 0);
    maze->fill_elem(x, y, dx, dy - 1, fill);
    maze->fill_elem(x, y + dy - 1, dx, 1, top);
  }

  void fill_ground_block(int x, int y, int dx, int dy)
  {
    fill_block_top(x, y, dx, dy, WALL_MIDDLE, WALL_SURFACE);
  }

  void fill_lava_block(int x, int y, int dx, int dy)
  {
    fill_block_top(x, y, dx, dy, LAVA_MIDDLE, LAVA_SURFACE);
  }

  void initial_floor_and_walls()
  {
    maze->fill_elem(0, 0, maze->w, maze->h, SPACE);

    maze->fill_elem(0, 0, maze->w, 1, WALL_SURFACE);
    maze->fill_elem(0, 0, 1, maze->h, WALL_MIDDLE);
    maze->fill_elem(maze->w - 1, 0, 1, maze->h, WALL_MIDDLE);
    maze->fill_elem(0, maze->h - 1, maze->w, 1, WALL_MIDDLE);

    maze->init_physics();
  }

  int randn(int n) {
    return rand_gen.randint(0, n);
  }

  float rand01() {
    return rand_gen.rand01();
  }

  char choose_crate() {
    return '#';
  }

  bool jump_and_build_platform_somewhere()
  {
    float gravity = GRAVITY;
    float max_jump = MAX_JUMP;
    float max_speed = MAX_SPEED;

    if (rec_stack.empty()) return false;
    int n = int(sqrt(randn(rec_stack.size() * rec_stack.size())));
    assert(n < (int)rec_stack.size());
    Rec r = rec_stack[n];
    float vx = (rand01()*2-1)*0.5*max_speed;
    float vy = (0.8 + 0.2*rand01())*max_jump;

    int top = 1 + int(vy/gravity);
    int ix, iy;
    if (randn(2)==1) {
      int steps = top + (randn(top/2));
      float x = r.x;
      float y = r.y + 1;

      ix = -1;
      iy = -1;
      for (int s=0; s<steps; s++) {
        vy -= gravity;
        x += vx;
        y += vy;
        if (ix != int(x) || iy != int(y)) {
          ix = int(x);
          iy = int(y);
          bool ouch = false;
          ouch |= ix<1;
          ouch |= ix>=maze->w-1;
          ouch |= iy<1;
          ouch |= iy>=maze->h-2;
          if (ouch) return false;
          char c = maze->get_elem(ix, iy);
          ouch |= c!=SPACE && c!=' ';
          if (ouch) return false;
          maze->set_elem(ix, iy, ' ');
        }
      }
    } else {
      ix = r.x;
      iy = r.y;
      if (iy >= maze->h - 3)
        return false;
      if (is_crat(maze->get_elem(ix, iy)) || is_crat(maze->get_elem(ix, iy-1)))
        return false; // don't build ladders starting from crates
      rec_stack.erase(rec_stack.begin()+n);
      std::vector<Rec> future_ladder;
      int ladder_len = 5 + randn(10);
      for (int s=0; s<ladder_len; s++) {
        future_ladder.push_back(Rec({ ix, iy }));
        iy += 1;
        bool ouch = false;
        ouch |= iy>=maze->h-3;
        ouch |= maze->get_elem(ix, iy) != SPACE;
        ouch |= maze->get_elem(ix-1, iy) == LADDER;
        ouch |= maze->get_elem(ix+1, iy) == LADDER;
        if (ouch) return false;
      }
      for (const Rec& f: future_ladder)
        maze->set_elem(f.x, f.y, LADDER);
      maze->set_elem(ix, iy, LADDER);
    }

    char c = maze->get_elem(ix, iy);
    if (iy >= maze->h - 3)
        return false;
    if (c==SPACE || c==' ')
      maze->set_elem(ix, iy, vx>0 ? 'a':'b');
    std::vector<Rec> crates;
    std::vector<Rec> monster_candidates;
    int len = 2 + randn(10);
    int crates_shift = randn(20);
    for (int platform=0; platform<len; platform++) {
      ix += (vx>0 ? +1 : -1);
      int c = maze->get_elem(ix, iy);
      if (c == ' ' || c == SPACE) {
        maze->set_elem(ix, iy, (platform<len-1) ? WALL_SURFACE : (vx>0 ? 'b':'a'));
        rec_stack.push_back(Rec({ ix, iy+1 }));
        if (int(ix*0.2 + iy + crates_shift) % 4 == 0)
          crates.push_back(Rec({ ix, iy+1 }));
        else if (platform>0 && platform<len-1)
          monster_candidates.push_back(Rec({ ix, iy+1 }));
      } else {
        if (c =='a' || c == 'b')
          maze->set_elem(ix, iy, WALL_SURFACE);
        break;
      }
    }

    if (monster_candidates.size() > 1) {
      const Rec& r = monster_candidates[randn(monster_candidates.size())];
      bool should_be_ground_monster = randn(10) >= 8;
      if (should_be_ground_monster) {
        maze->set_elem(r.x, r.y, GROUND_MONSTER);
      } else {
        maze->set_elem(r.x, r.y, WALKING_MONSTER);
      }
    }

    while (1) {
      int cnt = crates.size();
      if (cnt==0) break;
      for (int c=0; c<(int)crates.size(); ) {
        char w = maze->get_elem(crates[c].x, crates[c].y);
        char wl = maze->get_elem(crates[c].x-1, crates[c].y);
        char wr = maze->get_elem(crates[c].x+1, crates[c].y);
        char wu = maze->get_elem(crates[c].x, crates[c].y+1);
        int want = 2 + is_crat(wl) + is_crat(wr) - (wr==LADDER) - (wl==LADDER) - is_wall(wu);
        if (randn(4) < want && crates[c].y < maze->h-2) {
          if (w==' ' || w==SPACE)
            maze->set_elem(crates[c].x, crates[c].y, choose_crate());
          crates[c].y += 1;
          rec_stack.push_back(Rec({ crates[c].x, crates[c].y }));  // coins on crates, jumps from crates
          c++;
        } else {
          crates.erase(crates.begin() + c);
        }
      }
    }

    return true;
  }

  void place_coins()
  {
    int coins = 0;
    while (!rec_stack.empty()) {
      Rec r = rec_stack[rec_stack.size()-1];
      rec_stack.pop_back();
      int x = r.x;
      int y = r.y;
      bool good_place =
        (maze->get_elem(x, y) == SPACE ||
          maze->get_elem(x, y) == WALKING_MONSTER) &&
        r.y > 2 &&
        (maze->get_elem(x-1, y) == SPACE ||
          maze->get_elem(x-1, y) == WALKING_MONSTER) &&
        (maze->get_elem(x+1, y) == SPACE || 
          maze->get_elem(x+1, y) == WALKING_MONSTER) &&
        (maze->get_elem(x, (y+1)) == SPACE ||
          maze->get_elem(x, (y+1)) == WALKING_MONSTER) &&
        is_wall(maze->get_elem(x-1, y-1), true) &&
        is_wall(maze->get_elem(x, y-1), true) &&
        is_wall(maze->get_elem(x+1, y-1), true);
      if (good_place) {
        if (randn(10) >= 9) {
          maze->set_elem(x, y, COIN_OBJ2);
        } else {
          maze->set_elem(x, y, COIN_OBJ1);
        }
        coins += 1;
      }
    }
    maze->coins = coins;
  }

  void remove_traces_add_monsters()
  {
    maze->monsters.clear();
    for (int y=1; y<maze->h; ++y) {
      for (int x=1; x<maze->w-1; x++) {
        int& c = maze->get_elem(x, y);
        int& b = maze->get_elem(x, y-1);
        int cl = maze->get_elem(x-1, y);
        int cr = maze->get_elem(x+1, y);

        if (c==' ' && randn(20)==0 && !is_wall(b) && y>2) {
          maze->set_elem(x, y, FLYING_MONSTER);
        } else if (c==' ') {
          maze->set_elem(x, y, SPACE);
        }
        if ((c=='a' || c=='b') && is_wall(b))
          c = 'S';
        if (is_wall(c) && is_wall(b))
          b = 'A';
        if (c==FLYING_MONSTER || c==WALKING_MONSTER || c==GROUND_MONSTER) {
          std::shared_ptr<Monster> m(new Monster);
          m->x = x;
          m->y = y;
          for (int t=0; t<MONSTER_TRAIL; t++) {
            m->prev_x[t] = x;
            m->prev_y[t] = y;
          }
          m->is_flying = c==FLYING_MONSTER;
          m->is_walking = c==WALKING_MONSTER;

          std::vector<int> *type_theme_idxs;

          if (m->is_flying) {
            type_theme_idxs = &flying_theme_idxs;
          } else if (m->is_walking) {
            type_theme_idxs = &walking_theme_idxs;
          } else {
            type_theme_idxs = &ground_theme_idxs;
          }

          int chosen_idx = randn(type_theme_idxs->size());
          m->theme_n = (*type_theme_idxs)[chosen_idx];

          c = SPACE;

          if ((!m->is_walking || (!is_wall(cl) && !is_wall(cr))) && !(!m->is_flying && !is_wall(b))) 
          // walking monster should have some free space to move
          // walking monster and ground monster should be on a platform
            maze->monsters.push_back(m);
        }
      }
    }
  }

  void generate_test_level()
  {
    maze->spawnpos[0] = 2;
    maze->spawnpos[1] = 2;
    maze->coins = 0;
    for (int y=0; y<maze->h; ++y) {
      for (int x=0; x<maze->w; x++) {
        char c = test[maze->w*(maze->h-y-1) + x];
        if (is_coin(c)) maze->coins += 1;
          maze->set_elem(x, y, c);
      }
    }
    remove_traces_add_monsters();
  }

  void generate_coins_on_platforms()
  {
    maze->spawnpos[0] = 1 + randn(maze->w - 2);
    maze->spawnpos[1] = 1;

    for (int x=0; x<maze->w; x++) {
      rec_stack.push_back(Rec({ x, 1 }));
    }

    int want_platforms = 11;
    for (int p=0; p<want_platforms*10; p++) {
      bool success = jump_and_build_platform_somewhere();
      if (success) want_platforms -= 1;
      if (want_platforms==0) break;
    }
    place_coins();
    remove_traces_add_monsters();
  }
};

static QString resource_path;

struct PlayerTheme {
  QString theme_name;
  QImage stand;
  QImage front;
  QImage walk1;
  QImage walk2;
  QImage climb1;
  QImage climb2;
  QImage jump;
  QImage duck;
  QImage hit;
};

struct GroundTheme {
  QString theme_name;
  std::map<char, QImage> walls;
  QImage default_wall;
};

struct EnemyTheme {
  QString enemy_name;
  QImage walk1;
  QImage walk2;
  QImage dead;
  bool can_be_killed;
  float monster_max_speed = MONSTER_SPEED;
  bool is_jumping_monster = false;
  int max_pause_time = 0;
  float max_jump_height = 0;
  int anim_freq = 1;
};

static std::vector<GroundTheme> ground_themes;
static std::vector<PlayerTheme> player_themesl;
static std::vector<PlayerTheme> player_themesr;
static std::vector<EnemyTheme> enemy_themel;
static std::vector<EnemyTheme> enemy_themer;

static std::vector<GroundTheme> ground_themes_down;
static std::vector<PlayerTheme> player_themesl_down;
static std::vector<PlayerTheme> player_themesr_down;
static std::vector<EnemyTheme> enemy_themel_down;
static std::vector<EnemyTheme> enemy_themer_down;

static QImage power_up_shield;

static std::vector<QImage> bg_images;
static std::vector<QString> bg_images_fn;

static
QImage downsample(QImage img)
{
  int w = img.width();
  assert(w > 0);
  int h = img.height();

  return img.scaled(w / DOWNSAMPLE, h / DOWNSAMPLE, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

static
void ground_theme_downsample(GroundTheme *g, GroundTheme *d)
{
  for (const std::pair<char, QImage> &pair : g->walls) {
    d->walls[pair.first] = downsample(pair.second);
  }
  d->default_wall = downsample(g->default_wall);
}

static
void player_theme_downsample(PlayerTheme *theme, PlayerTheme *theme_down)
{
  theme_down->stand = downsample(theme->stand);
  theme_down->front = downsample(theme->front);
  theme_down->walk1 = downsample(theme->walk1);
  theme_down->walk2 = downsample(theme->walk2);
  theme_down->climb1 = downsample(theme->climb1);
  theme_down->climb2 = downsample(theme->climb2);
  theme_down->jump = downsample(theme->jump);
  theme_down->duck = downsample(theme->duck);
  theme_down->hit = downsample(theme->hit);
}

static
void enemy_theme_downsample(EnemyTheme* theme, EnemyTheme* theme_down)
{
  theme_down->walk1 = downsample(theme->walk1);
  theme_down->walk2 = downsample(theme->walk2);
}

static
PlayerTheme* choose_player_theme(int theme_n, bool is_facing_right, bool lowres)
{
  std::vector<PlayerTheme>* active_theme;

  if (lowres) {
    active_theme = is_facing_right ? &player_themesr_down : &player_themesl_down;
  } else {
    active_theme = is_facing_right ? &player_themesr : &player_themesl;
  }
  
  return &(*active_theme)[theme_n];
}

static
GroundTheme* choose_ground_theme(int theme_n, bool lowres)
{
  return lowres ? &ground_themes_down[theme_n] : &ground_themes[theme_n];
}

static
EnemyTheme* choose_enemy_theme(const std::shared_ptr<Monster>& m, int monster_n, bool lowres)
{
  if (lowres) {
    return m->vx>0 ? &enemy_themer_down[m->theme_n] : &enemy_themel_down[m->theme_n];
  } else {
    return m->vx>0 ? &enemy_themer[m->theme_n] : &enemy_themel[m->theme_n];
  }
}

QImage load_resource(QString relpath)
{
  auto path = resource_path + "/" + relpath;
  auto img = QImage(path);
  if (img.width() == 0) {
    fprintf(stderr, "failed to load image %s\n", path.toUtf8().constData());
    exit(EXIT_FAILURE);
  }
  return img;
}

void load_enemy_themes(const char **ethemes, std::vector<int> &type_theme_idxs, bool is_flying_type, bool is_walking_type) {
  for (const char **theme=ethemes; *theme; ++theme) {
    int curr_idx = enemy_themel.size();
    type_theme_idxs.push_back(curr_idx);

    QString dir = "kenneyLarge/Enemies/";
    EnemyTheme e1;
    e1.enemy_name = QString::fromUtf8(*theme);
    if (is_walking_type)
      e1.anim_freq = 5;
    if (strcmp(*theme, "snail") == 0) {
      e1.monster_max_speed = MONSTER_SPEED * 0.4;
    } else if (strcmp(*theme, "ladybug") == 0) {
      e1.monster_max_speed = MONSTER_SPEED * 1.8;
      e1.is_jumping_monster = true;
      e1.max_pause_time = 15;
      e1.max_jump_height = 0.08;
    } else if (strcmp(*theme, "wormPink") == 0) {
      e1.monster_max_speed = MONSTER_SPEED * 0.6;
    } else if (strcmp(*theme, "slimeBlock") == 0) {
      e1.monster_max_speed = MONSTER_SPEED * 1;
    } else if (strcmp(*theme, "slimeBlue") == 0) {
      e1.monster_max_speed = MONSTER_SPEED * 1;
    } else if (strcmp(*theme, "mouse") == 0) {
      e1.monster_max_speed = MONSTER_SPEED * 2.0;
    } else if (strcmp(*theme, "barnacle") == 0) {
      e1.anim_freq = 10;
    } else if (strcmp(*theme, "frog") == 0) {
      e1.is_jumping_monster = true;
      e1.monster_max_speed = MONSTER_SPEED * 2.0;
      e1.max_pause_time = 60;
      e1.max_jump_height = 0.2;
    }
    e1.can_be_killed = (strcmp(*theme, "slimeBlock") == 0 || strcmp(*theme, "snail") == 0 || strcmp(*theme, "wormPink") == 0);
    e1.walk1 = load_resource(dir + e1.enemy_name + ".png");
    e1.walk2 = load_resource(dir + e1.enemy_name + "_move.png");
    e1.dead = load_resource(dir + e1.enemy_name + "_dead.png");
    enemy_themel.push_back(e1);
    EnemyTheme e1d = e1;

    EnemyTheme e2 = e1;
    e2.walk1 = e2.walk1.mirrored(true, false);
    e2.walk2 = e2.walk2.mirrored(true, false);
    e2.dead = e2.dead.mirrored(true, false);
    EnemyTheme e2d = e2;
    enemy_themer.push_back(e2);

    enemy_theme_downsample(&e1, &e1d);
    enemy_theme_downsample(&e2, &e2d);
    enemy_themel_down.push_back(e1d);
    enemy_themer_down.push_back(e2d);
  }
}


static const char *bgthemes[] = {
  // Many other backgrounds available at backgrounds/ and kenny/Backgrounds/
  "backgrounds/background-2/airadventurelevel4.png",
  "backgrounds/spacebackgrounds-0/milky_way_01.png",
  0};

static const char *gthemes[] = {
  // Other supported ground themes are Dirt, Grass, Sand, Stone
  "Snow",
  "Planet",
  0};

static const char *pthemes[] = {
  // Other alien themes are Beige, Blue, Green, Pink
  "Yellow",
  0};

static const char *ground_monsters[] = {
  "sawHalf",
  "barnacle",
  0};

static const char *flying_monsters[] = {
  "bee",
  0};

static const char *walking_monsters[] = {
  "slimeBlock",
  "slimeBlue",
  "mouse",
  "snail",
  "ladybug",
  "wormPink",
  "frog",
  0};

void images_load()
{
  for (const char **theme=bgthemes; *theme; ++theme) {
    QString path = QString::fromUtf8(*theme);
    bg_images.push_back(load_resource(path));
    bg_images_fn.push_back(path);
  }

  for (const char **theme=gthemes; *theme; ++theme) {
    GroundTheme t;
    GroundTheme td;
    t.theme_name = QString::fromUtf8(*theme);
    QString walls = "kenney/Ground/" + t.theme_name + "/" + t.theme_name.toLower();
    t.default_wall = load_resource(walls + "Center.png"); 
    t.walls['a'] = load_resource(walls + "Cliff_left.png");
    t.walls['b'] = load_resource(walls + "Cliff_right.png");
    t.walls[WALL_SURFACE] = load_resource(walls + "Mid.png");
    t.walls['^'] = load_resource(walls + "Half_mid.png");
    QString items = "kenneyLarge/Items/";
    t.walls[' '] = load_resource(items + "star.png");
    t.walls[COIN_OBJ1] = load_resource(items + "coinGold.png");
    t.walls[COIN_OBJ2] = load_resource(items + "gemRed.png");
    QString tiles = "kenney/Tiles/";
    t.walls['#'] = load_resource(tiles + "boxCrate.png");
    t.walls['$'] = load_resource(tiles + "boxCrate_double.png");
    t.walls['&'] = load_resource(tiles + "boxCrate_single.png");
    t.walls['%'] = load_resource(tiles + "boxCrate_warning.png");
    t.walls[LAVA_MIDDLE] = load_resource(tiles + "lava.png");
    t.walls[LAVA_SURFACE] = load_resource(tiles + "lavaTop_low.png");
    t.walls[SPIKE_OBJ] = load_resource(tiles + "spikes.png");
    t.walls[LADDER] = load_resource(tiles + "ladderMid.png");
    ground_themes.push_back(t);
    ground_theme_downsample(&t, &td);
    ground_themes_down.push_back(td);
  }

  for (const char **theme=pthemes; *theme; ++theme) {
    PlayerTheme t1;
    PlayerTheme t1d;
    t1.theme_name = QString::fromUtf8(*theme);
    // We removed Mugen's helmet for aesthetic reasons
    QString dir = "kenneyLarge/Players/128x256_no_helmet/" + t1.theme_name + "/alien" + t1.theme_name;
    t1.stand = load_resource(dir + "_stand.png");
    t1.front = load_resource(dir + "_front.png");
    t1.walk1 = load_resource(dir + "_walk1.png");
    t1.walk2 = load_resource(dir + "_walk2.png");
    t1.climb1 = load_resource(dir + "_climb1.png");
    t1.climb2 = load_resource(dir + "_climb2.png");
    t1.jump = load_resource(dir + "_jump.png");
    t1.duck = load_resource(dir + "_duck.png");
    t1.hit = load_resource(dir + "_hit.png");
    player_themesr.push_back(t1);

    PlayerTheme t2;
    PlayerTheme t2d;
    t2.theme_name = QString::fromUtf8(*theme);
    t2.stand = t1.stand.mirrored(true, false);
    t2.front = t1.front.mirrored(true, false);
    t2.walk1 = t1.walk1.mirrored(true, false);
    t2.walk2 = t1.walk2.mirrored(true, false);
    t2.climb1 = t1.climb1.mirrored(true, false);
    t2.climb2 = t1.climb2.mirrored(true, false);
    t2.jump = t1.jump.mirrored(true, false);
    t2.duck = t1.duck.mirrored(true, false);
    t2.hit = t1.hit.mirrored(true, false);
    player_themesl.push_back(t2);

    player_theme_downsample(&t1, &t1d);
    player_themesr_down.push_back(t1d);
    player_theme_downsample(&t2, &t2d);
    player_themesl_down.push_back(t2d);
  }

  // load power up agent assets
  power_up_shield = load_resource("bubble_shield.png");

  // load enemy themes
  load_enemy_themes(ground_monsters, ground_theme_idxs, false, false);
  load_enemy_themes(walking_monsters, walking_theme_idxs, false, true);
  load_enemy_themes(flying_monsters, flying_theme_idxs, true, false);
}

void monitor_csv_save_string(FILE* monitor_csv, const char* c_str) {
  if (!monitor_csv)
    return;
  fprintf(monitor_csv, "%s\n", c_str);
  fflush(monitor_csv);
}

struct Agent {
  std::shared_ptr<Maze> maze;
  int theme_n;
  float x, y, vx, vy;
  float spring = 0;
  float zoom = 1.0;
  float target_zoom = 1.0;
  uint8_t render_buf[RES_W*RES_H*4];
  uint8_t* audio_seg_map_buf = 0;
  uint8_t* render_hires_buf = 0;
  bool game_over = false;
  float reward = 0;
  float reward_sum = 0;
  bool is_facing_right;
  bool ladder_mode;
  int action_dx = 0;
  int action_dy = 0;
  int time_alive;
  bool is_killed = false;
  bool is_preparing_to_jump = false;
  bool killed_monster = false;
  bool bumped_head = false;
  int killed_animation_frame_cnt = 0;
  int finished_level_frame_cnt = 0;
  bool power_up_mode = false;
  bool collected_coin = false;
  bool collected_gem = false;
  bool collect_data;
  bool support;
  FILE *monitor_csv = 0;
  double t0;

  ~Agent() {
    if (render_hires_buf) {
      delete[] render_hires_buf;
      render_hires_buf = 0;
    }
    if (audio_seg_map_buf) {
      delete[] audio_seg_map_buf;
      audio_seg_map_buf = 0;
    }
    if (monitor_csv) {
      fclose(monitor_csv);
      monitor_csv = 0;
    }
  }

  void monitor_csv_open(int n_in_vec) {
    t0 = get_time();
    std::string monitor_fn;
    char *rank_ch = getenv("PMI_RANK");
    if (rank_ch) {
      int rank = atoi(rank_ch);
      monitor_fn = monitor_dir + stdprintf("/%02i%02i.monitor.csv", rank, n_in_vec);
    } else {
      monitor_fn = monitor_dir + stdprintf("/%03i.monitor.csv", n_in_vec);
    }
    monitor_csv = fopen(monitor_fn.c_str(), "wt");
    std::cout << "csv file location: " << monitor_fn.c_str() << std::endl;
    fprintf(monitor_csv, "# {\"t_start\": %0.2lf, \"gym_version\": \"coinrun\", \"env_id\": \"coinrun\"}\n", t0);
    // fprintf(monitor_csv, "r,l,t\n");
    fflush(monitor_csv);

    // save global generation parameters to be able to reproduce later
    std::stringstream buffer;
    buffer << "background_themes";
    for (const char **theme=bgthemes; *theme; ++theme) {
      buffer << "," << *theme;
    }
    buffer << std::endl << "ground_themes";
    for (const char **theme=gthemes; *theme; ++theme) {
      buffer << "," << *theme;
    }
    buffer << std::endl << "agent_themes";
    for (const char **theme=pthemes; *theme; ++theme) {
      buffer << "," << *theme;
    }
    buffer << std::endl << "ground_monsters";
    for (const char **theme=ground_monsters; *theme; ++theme) {
      buffer << "," << *theme;
    }
    buffer << std::endl << "flying_monsters";
    for (const char **theme=flying_monsters; *theme; ++theme) {
      buffer << "," << *theme;
    }
    buffer << std::endl << "walking_monsters";
    for (const char **theme=walking_monsters; *theme; ++theme) {
      buffer << "," << *theme;
    }
    buffer << std::endl;

    monitor_csv_save_string(monitor_csv, buffer.str().c_str());
  }

  void monitor_csv_episode_over() {
    if (!monitor_csv)
      return;
    fprintf(monitor_csv, "episode_over,%0.1f,%i,%0.1f\n", reward_sum, time_alive, get_time() - t0);
    fflush(monitor_csv);
  }

  void reset(int spawn_n) {
    x = maze->spawnpos[0];
    y = maze->spawnpos[1];
    action_dx = 0;
    action_dy = 0;
    time_alive = 0;
    reward_sum = 0;
    vx = vy = spring = 0;
    is_facing_right = true;
  }

  void eat_coin(int x, int y)
  {
    int obj = maze->get_elem(x, y);
    bool eat_coin_to_save = false;

    if (is_lethal(obj)) {
      maze->is_terminated = true;
      is_killed = true;
      killed_animation_frame_cnt = DEATH_ANIM_LENGTH;
    }

    if (is_coin(obj)) {
      maze->set_elem(x, y, SPACE);
      maze->coins -= 1;
      collected_coin = true;
      eat_coin_to_save = true;
      if (power_up_mode)
        power_up_mode = false;

      if (maze->coins == 0) {
        reward += 10.0f;
        reward_sum += 10.0f;
        maze->is_terminated = true;
        finished_level_frame_cnt = FINISHED_LEVEL_ANIM_LENGTH;
      } else {
        reward += 1.0f;
        reward_sum += 1.0f;
      }
    }

    if (is_gem(obj)) {
      maze->set_elem(x, y, SPACE);
      eat_coin_to_save = true;
      reward += 1.0f;
      reward_sum += 1.0f;
      power_up_mode = true;
      collected_gem = true;
    }

    if (eat_coin_to_save) {
      std::stringstream buffer;
      buffer << "eat_coin," << x << "," << y << std::endl;
      monitor_csv_save_string(monitor_csv, buffer.str().c_str());
    }
  }

  void sub_step(float _vx, float _vy)
  {
    float ny = y + _vy;
    float nx = x + _vx;

    if (_vy < 0 && !maze->has_vertical_space(x, ny, false)) {
      y = int(ny) + 1;
      support = true;
      vy = 0;

    } else if (_vy < 0 && !maze->has_vertical_space(x, ny, true)) {
      if (action_dy >= 0 && int(ny)!=int(y)) {
        y = int(ny) + 1;
        vy = 0;
        support = true;
      } else {  // action_dy < 0, come down from a crate
        support = false;
        y = ny;
      }

    } else if (_vy > 0 && !maze->has_vertical_space(x, ny + 1, false)) {
      y = int(ny);
      while (!maze->has_vertical_space(x, y, false)) {
        y -= 1;
      }
      bumped_head = true;
      vy = 0;
      reward -= BUMP_HEAD_PENALTY;
      reward_sum -= BUMP_HEAD_PENALTY;

    } else {
      y = ny;
    }

    int ix = int(x);
    int iy = int(y);
    int inx = int(nx);

    if (_vx < 0 && is_wall(maze->get_elem(inx, iy)) ) {
      vx = 0;
      x = int(inx) + 1;
    } else if (_vx > 0 && is_wall(maze->get_elem(inx + 1, iy))) {
      vx = 0;
      x = int(inx);
    } else {
      x = nx;
    }

    eat_coin(ix, iy);
    eat_coin(ix, iy+1);
    eat_coin(ix+1, iy);
    eat_coin(ix+1, iy+1);
  }

  void step_coinrun()
  {
    support = false;
    if (finished_level_frame_cnt > 0) {
      action_dy = 0;
      action_dx = 0;
    }

    int near_x = int(x + .5);
    char test_is_ladder1 = maze->get_elem(near_x, int(y + 0.2));
    char test_is_ladder2 = maze->get_elem(near_x, int(y - 0.2));

    if (test_is_ladder1 == LADDER || test_is_ladder2 == LADDER) {
      if (action_dy != 0)
        ladder_mode = true;
    } else {
      ladder_mode = false;
    }

    float max_jump = maze->max_jump;
    float max_speed = maze->max_speed;
    float mix_rate = maze->mix_rate;

    if (ladder_mode) {
      vx = (1-LADDER_MIXRATE_X)*vx + LADDER_MIXRATE_X*max_speed*(action_dx + 0.2*(near_x - x));
      vx = clip_abs(vx, LADDER_V);
      vy = (1-LADDER_MIXRATE_Y)*vy + LADDER_MIXRATE_Y*max_speed*action_dy;
      vy = clip_abs(vy, LADDER_V);

    } else if (spring > 0 && vy==0 && action_dy==0) {
      vy = max_jump;
      reward -= JUMP_PENALTY;
      reward_sum -= JUMP_PENALTY;

      spring = 0;
      support = true;
    } else {
      vy -= maze->gravity;
    }

    vy = clip_abs(vy, max_jump);
    vx = clip_abs(vx, max_speed);

    int num_sub_steps = 2;
    float pct = 1.0 / num_sub_steps;

    for (int s = 0; s < num_sub_steps; s++) {
      sub_step(vx * pct, vy * pct);
      if (vx == 0 && vy == 0) {
        break;
      }
    }

    if (support) {
      if (action_dy > 0)
        spring += sign(action_dy) * max_jump/4; // four jump heights
      if (action_dy < 0)
        spring = -0.01;
      if (action_dy == 0 && spring < 0)
        spring = 0;

      spring = clip_abs(spring, max_jump);
      vx = (1-mix_rate)*vx;
      if (spring==0) vx += mix_rate*max_speed*action_dx;
      if (fabs(vx) < mix_rate*max_speed) vx = 0;

    } else {
      spring = 0;
      float ac = maze->air_control;
      vx = (1-ac*mix_rate)*vx + ac*mix_rate*action_dx;
    }

    if (vx < 0) {
      is_facing_right = false;
    } else if (vx > 0) {
      is_facing_right = true;
    }

    if (spring != 0 && !(is_killed || ladder_mode || vy != 0)) {
      reward -= SQUAT_PENALTY;
      reward_sum -= SQUAT_PENALTY;
      is_preparing_to_jump = true;
    } else {
      if (is_preparing_to_jump && vy != max_jump) {
        reward -= JITTER_SQUAT_PENALTY;
        reward_sum -= JITTER_SQUAT_PENALTY;
      }
      is_preparing_to_jump = false;
    }
  }

  void step()
  {
    time_alive += 1;
    int timeout = 0;

    timeout = LEVEL_TIMEOUT;
    step_coinrun();

    if (time_alive > timeout) {
        maze->is_terminated = true;
    }
  }

  QImage picture(PlayerTheme *theme) const
  {
    if (is_killed) 
      return theme->hit;
    if (ladder_mode)
      return (time_alive / 5 % 2 == 0) ? theme->climb1 : theme->climb2;
    if (vy != 0)
      return theme->jump;
    if (spring != 0)
      return theme->duck;
    if (vx == 0)
      return theme->stand;

    return (time_alive / 5 % 2 == 0) ? theme->walk1 : theme->walk2;
  }
};

void Monster::step(const std::shared_ptr<Maze>& maze)
{
  if (!is_flying && !is_walking)
  return;
  float control = sign(vx);
  int ix = int(x);
  int iy = int(y);
  int look_left  = maze->get_elem(ix-0, iy);
  int look_right = maze->get_elem(ix+1, iy);
  if (is_wall(look_left)) control = +1;
  if (is_wall(look_right)) control = -1;
  if (is_walking) {
    int feel_left  = maze->get_elem(ix-0, iy-1);
    int feel_right = maze->get_elem(ix+1, iy-1);
    if (!is_wall(feel_left)) control = +1;
    if (!is_wall(feel_right)) control = -1;
  }

  float monster_max_speed = enemy_themel[theme_n].monster_max_speed;
  vx = clip_abs(MONSTER_MIXRATE*control + (1-MONSTER_MIXRATE)*vx, monster_max_speed);

  bool is_jumping_monster = enemy_themel[theme_n].is_jumping_monster;
  if (is_jumping_monster) {
    if (vy == 0 && pause == 0) {
      // time to jump!
      vy = enemy_themel[theme_n].max_jump_height;
    } else if (pause == 0) {
      // falling due to gravity
      vy -= 0.8 * maze->gravity;
    }

    float ny = y + vy;
    if (vy < 0 && !maze->has_vertical_space(x, ny, false)) {
      y = int(ny) + 1;
      vy = 0;
      // pause based on some random choice
      pause = global_rand_gen.randint(0, enemy_themel[theme_n].max_pause_time);
    } 
  }


  if (pause > 0) {
    pause -= 1;
  } else {
    x += vx;
    y += vy;
  }
  for (int t=1; t<MONSTER_TRAIL; t++) {
    prev_x[t-1] = prev_x[t];
    prev_y[t-1] = prev_y[t];
  }
  prev_x[MONSTER_TRAIL-1] = x;
  prev_y[MONSTER_TRAIL-1] = y;
}

struct State {
  int state_n; // in vstate
  std::shared_ptr<Maze> maze;
  int world_theme_n;
  State(const std::shared_ptr<VectorOfStates>& belongs_to):
    belongs_to(belongs_to)  { }

  QMutex state_mutex;
   std::weak_ptr<VectorOfStates> belongs_to;
   int time;
   int game_id = -1;
   Agent agent;

  QMutex step_mutex;
   bool step_in_progress = false;
   bool agent_ready = false;
};

void state_reset(const std::shared_ptr<State>& state)
{
  assert(player_themesl.size() > 0 && "Please call init(threads) first");

  int level_seed = 0;

  if (USE_LEVEL_SET) {
    int level_index = global_rand_gen.randint(0, NUM_LEVELS);
    level_seed = LEVEL_SEEDS[level_index];
  } else if (NUM_LEVELS > 0) {
    level_seed = global_rand_gen.randint(0, NUM_LEVELS);
  } else {
    level_seed = global_rand_gen.randint();
  }

  RandomMazeGenerator maze_gen;
  maze_gen.rand_gen.seed(level_seed);

  int w = 64;
  int h = 13;
  state->maze.reset(new Maze(w, h));
  maze_gen.maze = state->maze;

  maze_gen.initial_floor_and_walls();

  maze_gen.generate_coins_on_platforms();

  Agent &agent = state->agent;
  float zoom = state->maze->default_zoom;
  agent.maze = state->maze;
  agent.zoom = zoom;
  agent.target_zoom = zoom;

  agent.theme_n = maze_gen.randn(player_themesl.size());
  state->world_theme_n = maze_gen.randn(ground_themes.size());

  agent.reset(0);

  state->maze->is_terminated = false;
  state->agent.is_killed = false;
  state->agent.is_preparing_to_jump = false;
  state->agent.killed_monster = false;
  state->agent.bumped_head = false;
  state->agent.killed_animation_frame_cnt = 0;
  state->agent.finished_level_frame_cnt = 0;
  state->agent.power_up_mode = false;
  state->time = 0;
  state->game_id += 1;

  std::stringstream buffer;
  buffer << "game_id,maze_seed,zoom,world_theme_n,agent_theme_n" << std::endl;
  buffer << state->game_id << "," << level_seed << "," << agent.zoom << "," << state->world_theme_n << "," << agent.theme_n << std::endl;
  for (int y=0; y<h; y++) {
    for (int x=0; x<w; x++) {
      int wkey = state->maze->get_elem(x, y);
      char wkey_ch = wkey;
      buffer << wkey_ch << ",";
    }
  }
  buffer << std::endl;

  monitor_csv_save_string(agent.monitor_csv, buffer.str().c_str());
}

// -- render --

static
int to_shade(float f)
{
  int shade = int(f * 255);
  if (shade < 0) shade = 0;
  if (shade > 255) shade = 255;
  return shade;
}

static
void paint_the_world_for_agent(
  QPainter& p, const QRect& rect,
  const std::shared_ptr<State>& state, const Agent* agent)
{
  // This function paints the 64x64 frame that is input for the agent. Some notable differences
  // between this frame and the video frame are resolution (64x64 vs 1024x1024), monsters have
  // trails in this frame so the agent knows their direction, a velocity patch is placed in the
  // corner of the agent frame so it knows its own velocity. 

  const double zoom = 5.0;
  const double bgzoom = 0.4;

  bool lowres = rect.height() < 200;
  GroundTheme* ground_theme = choose_ground_theme(state->world_theme_n, lowres);

  std::shared_ptr<Maze> maze = agent->maze;

  double kx = zoom * rect.width()  / double(64);  // not w!
  double ky = zoom * rect.height() / double(64);
  double dx = (-agent->x) * kx + rect.center().x()  - 0.5*kx;
  double dy = (agent->y) * ky - rect.center().y()   - 0.5*ky;

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  p.setRenderHint(QPainter::HighQualityAntialiasing, true);

  for (int tile_x=-1; tile_x<=2; tile_x++) {
    for (int tile_y=-1; tile_y<=1; tile_y++) {
      double zx = rect.width()*zoom;   // / bgzoom;
      double zy = rect.height()*zoom;  // / bgzoom);
      QRectF bg_image = QRectF(0, 0, zx, zy);
      bg_image.moveCenter(QPointF(
        zx*tile_x + rect.center().x() + bgzoom*(dx + kx*maze->h/2),
        zy*tile_y + rect.center().y() + bgzoom*(dy - ky*maze->h/2)
        ));

      p.fillRect(bg_image, QColor(30, 30, 30));
      
    }
  }

  int radius = int(1 + 64 / zoom);  // actually /2 works except near scroll limits
  int ix = int(agent->x + .5);
  int iy = int(agent->y + .5);
  int x_start = max(ix - radius, 0);
  int x_end = min(ix + radius + 1, maze->w);
  int y_start = max(iy - radius, 0);
  int y_end = min(iy + radius + 1, maze->h);
  double WINH = rect.height();

  for (int y=y_start; y<y_end; ++y) {
    for (int x=x_start; x<x_end; x++) {
      int wkey = maze->get_elem(x, y);
      if (wkey==SPACE) continue;

      auto f = ground_theme->walls.find(wkey);
      QImage img = f == ground_theme->walls.end() ? ground_theme->default_wall : f->second;
      QRectF dst = QRectF(kx*x + dx, WINH - ky*y + dy, kx + .5, ky + .5);
      dst.adjust(-0.1, -0.1, +0.1, +0.1); // here an attempt to fix subpixel seams that appear on the image, especially lowres
      
      if (wkey==LAVA_MIDDLE || wkey==LAVA_SURFACE) {
        QRectF d1 = dst;
        QRectF d2 = dst;
        QRectF sr(QPointF(0,0), img.size());
        QRectF sr1 = sr;
        QRectF sr2 = sr;
        float tr = state->time*0.1;
        tr -= int(tr);
        tr *= -1;
        d1.translate(tr*dst.width(), 0);
        d2.translate(dst.width() + tr*dst.width(), 0);
        sr1.translate(-tr*img.width(), 0);
        sr2.translate(-img.width() - tr*img.width(), 0);
        d1 &= dst;
        d2 &= dst;
        d1.adjust(0, 0, +0.5, 0);
        d2.adjust(-0.5, 0, 0, 0);
        sr1 &= sr;
        sr2 &= sr;
        if (!sr1.isEmpty())
          p.drawImage(d1, img, sr1);
        if (!sr2.isEmpty())
          p.drawImage(d2, img, sr2);
      } else {
        p.drawImage(dst, img);
      }
    }
  }

  PlayerTheme* active_theme = choose_player_theme(agent->theme_n, agent->is_facing_right, lowres);
  QImage img = agent->picture(active_theme);
  if (agent->power_up_mode) {
    for (int x = 0; x < img.width(); x++) {
      for (int y = 0; y < img.height(); y++) {
        QColor pixel_color = img.pixelColor(x, y);
        pixel_color.setRgb(pixel_color.blue(), pixel_color.red(), pixel_color.green(), pixel_color.alpha());
        img.setPixelColor(x, y, pixel_color);
      }
    }
  }
  QRectF dst = QRectF(kx * agent->x + dx, WINH - ky * (agent->y+1) + dy, kx, 2 * ky);
  p.drawImage(dst, img);  
  
  int monsters_count = maze->monsters.size();
  for (int i=0; i<monsters_count; ++i) {
    const std::shared_ptr<Monster>& m = maze->monsters[i];
    QRectF dst = QRectF(kx*m->x + dx, WINH - ky*m->y + dy, kx, ky);

    EnemyTheme* theme = choose_enemy_theme(m, i, lowres);
    if ((m->is_flying || m->is_walking) && !m->is_dead) {
      for (int t=2; t<MONSTER_TRAIL; t+=2) {
        QRectF dst = QRectF(kx*m->prev_x[t] + dx, WINH - ky*m->prev_y[t] + dy, kx, ky);
        float ft = 1 - float(t)/MONSTER_TRAIL;
        float smaller = 0.20;
        float lower = -0.22;
        float soar = -0.4;
        dst.adjust(
          (smaller-0.2*ft)*kx, (soar*ft-0.2*ft-lower+smaller)*ky,
          (-smaller+0.2*ft)*kx, (soar*ft+0.2*ft-lower-smaller)*ky);
        p.setBrush(QColor(255,255,255, t*127/MONSTER_TRAIL));
        p.setPen(Qt::NoPen);
        p.drawEllipse(dst);
      }
    }
    QImage monster_image;
    if (m->is_dead) {
      monster_image = theme->dead;
      m->monster_dying_frame_cnt = max(0, m->monster_dying_frame_cnt);
      double monster_shrinkage = (MONSTER_DEATH_ANIM_LENGTH - m->monster_dying_frame_cnt) * 0.8 / MONSTER_DEATH_ANIM_LENGTH;
      dst = QRectF(kx*m->x + dx, WINH - ky*m->y + dy + ky * monster_shrinkage, kx, ky * (1 - monster_shrinkage));
      m->monster_dying_frame_cnt -= 1;
    } else if (theme->is_jumping_monster) {
      monster_image = m->vy == 0 ? theme->walk1 : theme->walk2;
    } else {
      monster_image = state->time / theme->anim_freq % 2 == 0 ? theme->walk1 : theme->walk2;
    }
    p.drawImage(dst, monster_image);
  }

  if (USE_DATA_AUGMENTATION) {
    float max_rand_dim = .25;
    float min_rand_dim = .1;
    int num_blotches = global_rand_gen.randint(0, 6);

    bool hard_blotches = false;

    if (hard_blotches) {
      max_rand_dim = .3;
      min_rand_dim = .2;
      num_blotches = global_rand_gen.randint(0, 10);
    }

    for (int j = 0; j < num_blotches; j++) {
      float rx = global_rand_gen.rand01() * rect.width();
      float ry = global_rand_gen.rand01() * rect.height();
      float rdx = (global_rand_gen.rand01() * max_rand_dim + min_rand_dim) * rect.width();
      float rdy = (global_rand_gen.rand01() * max_rand_dim + min_rand_dim) * rect.height();

      QRectF dst3 = QRectF(rx, ry, rdx, rdy);
      p.fillRect(dst3, QColor(global_rand_gen.randint(0, 255), global_rand_gen.randint(0, 255), global_rand_gen.randint(0, 255)));
    }
  }

  if (PAINT_VEL_INFO) {
    float infodim = rect.height() * .2;
    QRectF dst2 = QRectF(0, 0, infodim, infodim);
    int s0 = to_shade(agent->spring / maze->max_jump);
    int s1 = to_shade(.5 * agent->vx / maze->max_speed + .5);
    int s2 = to_shade(.5 * agent->vy / maze->max_jump + .5);
    p.fillRect(dst2, QColor(s1, s1, s1));

    QRectF dst3 = QRectF(infodim, 0, infodim, infodim);
    p.fillRect(dst3, QColor(s2, s2, s2));
  }
}

static
void paint_the_world_for_video_data(
  QPainter& p, const QRect& rect,
  const std::shared_ptr<State>& state, const Agent* agent)
{
  const_cast<Agent*>(agent)->zoom = 0.9*agent->zoom + 0.1*agent->target_zoom;
  double zoom = agent->zoom;
  const double bgzoom = 0.4;

  bool lowres = rect.height() < 200;
  GroundTheme* ground_theme = choose_ground_theme(state->world_theme_n, lowres);

  std::shared_ptr<Maze> maze = agent->maze;


  double kx = zoom * rect.width()  / double(64);  // not w!
  double ky = zoom * rect.height() / double(64);
  double dx = (-agent->x) * kx + rect.center().x()  - 0.5*kx;
  double dy = -rect.center().y()  + 5.0*ky;
  double alien_y = rect.height() - ky * (agent->y+1) + dy;

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  p.setRenderHint(QPainter::HighQualityAntialiasing, true);

  for (int tile_x=-1; tile_x<=2; tile_x++) {
    for (int tile_y=-1; tile_y<=1; tile_y++) {
      double zx = rect.width()*zoom;   // / bgzoom;
      double zy = rect.height()*zoom;  // / bgzoom);
      QRectF bg_image = QRectF(0, 0, zx, zy);
      bg_image.moveCenter(QPointF(
        zx*tile_x + rect.center().x() + bgzoom*(dx + kx*maze->h/2),
        zy*tile_y + rect.center().y() + bgzoom*(dy - ky*maze->h/2)
        ));

      p.drawImage(bg_image, bg_images[state->world_theme_n]);
    }
  }

  int radius = int(1 + 64 / zoom);  // actually /2 works except near scroll limits
  int ix = int(agent->x + .5);
  int iy = int(agent->y + .5);
  int x_start = max(ix - radius, 0);
  int x_end = min(ix + radius + 1, maze->w);
  int y_start = max(iy - radius, 0);
  int y_end = min(iy + radius + 1, maze->h);
  double WINH = rect.height();

  for (int y=y_start; y<y_end; ++y) {
    for (int x=x_start; x<x_end; x++) {
      int wkey = maze->get_elem(x, y);
      if (wkey==SPACE) continue;

      auto f = ground_theme->walls.find(wkey);
      QImage img = f == ground_theme->walls.end() ? ground_theme->default_wall : f->second;
      QRectF dst = QRectF(kx*x + dx, WINH - ky*y + dy, kx + .5, ky + .5);
      dst.adjust(-0.1, -0.1, +0.1, +0.1); // here an attempt to fix subpixel seams that appear on the image, especially lowres
      
      if (wkey==LAVA_MIDDLE || wkey==LAVA_SURFACE) {
        QRectF d1 = dst;
        QRectF d2 = dst;
        QRectF sr(QPointF(0,0), img.size());
        QRectF sr1 = sr;
        QRectF sr2 = sr;
        float tr = state->time*0.1;
        tr -= int(tr);
        tr *= -1;
        d1.translate(tr*dst.width(), 0);
        d2.translate(dst.width() + tr*dst.width(), 0);
        sr1.translate(-tr*img.width(), 0);
        sr2.translate(-img.width() - tr*img.width(), 0);
        d1 &= dst;
        d2 &= dst;
        d1.adjust(0, 0, +0.5, 0);
        d2.adjust(-0.5, 0, 0, 0);
        sr1 &= sr;
        sr2 &= sr;
        if (!sr1.isEmpty())
          p.drawImage(d1, img, sr1);
        if (!sr2.isEmpty())
          p.drawImage(d2, img, sr2);
      } else {
        p.drawImage(dst, img);
      }
    }
  }

  
  // save states to monitor.csv for converting to json metadata
  std::stringstream buffer;

  // save frame and agent information
  buffer << "time_alive,agent_x,agent_y,agent_vx,agent_vy,agent_facing_right,agent_ladder,agent_spring,is_killed,killed_animation_frame_cnt,finished_level_frame_cnt,killed_monster,bumped_head,collected_coin,collected_gem,power_up_mode" << std::endl;
  buffer << agent->time_alive << "," << agent->x << "," << agent->y << "," << agent->vx << "," << agent->vy << ",";
  buffer << agent->is_facing_right << "," << agent->ladder_mode << "," << agent->spring << ",";
  buffer << agent->is_killed << "," << agent->killed_animation_frame_cnt << "," << agent->finished_level_frame_cnt << ",";
  buffer << agent->killed_monster << "," << agent->bumped_head << "," << agent->collected_coin << ",";
  buffer << agent->collected_gem << "," << agent->power_up_mode << std::endl;

  int monsters_count = maze->monsters.size();
  // save general monster information
  buffer << "state_time,monsters_count,m_id,m_x,m_y,m_vx,m_vy,m_theme,m_flying,m_walking,m_jumping,m_dead,m_anim_freq,monster_dying_frame_cnt" << std::endl;
  buffer << state->time << "," << monsters_count << ",";
  for (int i=0; i<monsters_count; ++i) {
    const std::shared_ptr<Monster>& m = maze->monsters[i];
    QRectF dst = QRectF(kx*m->x + dx, WINH - ky*m->y + dy, kx, ky);

    EnemyTheme* theme = choose_enemy_theme(m, i, lowres);

    // save individual monster information, do this before monster_dying_frame_cnt may decrease
    buffer << i << "," << m->x << "," << m->y << "," << m->vx << "," << m->vy << "," << m->theme_n << ",";
    buffer << m->is_flying << "," << m->is_walking << "," << theme->is_jumping_monster << ",";
    buffer << m->is_dead << "," << theme->anim_freq << "," << m->monster_dying_frame_cnt << ",";
  
    QImage monster_image;
    if (m->is_dead) {
      monster_image = theme->dead;
      m->monster_dying_frame_cnt = max(0, m->monster_dying_frame_cnt);
      double monster_shrinkage = (MONSTER_DEATH_ANIM_LENGTH - m->monster_dying_frame_cnt) * 0.8 / MONSTER_DEATH_ANIM_LENGTH;
      dst = QRectF(kx*m->x + dx, WINH - ky*m->y + dy + ky * monster_shrinkage, kx, ky * (1 - monster_shrinkage));
    } else if (theme->is_jumping_monster) {
      monster_image = m->vy == 0 ? theme->walk1 : theme->walk2;
    } else {
      monster_image = state->time / theme->anim_freq % 2 == 0 ? theme->walk1 : theme->walk2;
    }
    p.drawImage(dst, monster_image);
  }
  buffer << std::endl;


  PlayerTheme* active_theme = choose_player_theme(agent->theme_n, agent->is_facing_right, lowres);
  QImage img = agent->picture(active_theme);
  if (agent->is_killed && agent->collect_data) {
    for (int x = 0; x < img.width(); x++) {
      for (int y = 0; y < img.height(); y++) {
        QColor pixel_color = img.pixelColor(x, y);
        int hue = pixel_color.hue();
        int saturation = max(pixel_color.saturation()-(DEATH_ANIM_LENGTH + 1 - agent->killed_animation_frame_cnt)*12, 0);
        int value = max(pixel_color.value(), 0);
        int alpha = max(pixel_color.alpha()-(DEATH_ANIM_LENGTH + 1 - agent->killed_animation_frame_cnt)*12, 0);

        pixel_color.setHsv(hue, saturation, value, alpha);
        img.setPixelColor(x, y, pixel_color);
      }
    }
  }
  QRectF dst = QRectF(kx * agent->x + dx, WINH - ky * (agent->y+1) + dy, kx, 2 * ky);
  p.drawImage(dst, img);  

  if (agent->power_up_mode) {
    QRectF bubble_dst = QRectF(kx * agent->x + dx - 7, WINH - ky * (agent->y+1) + dy + 8, kx * 1.15, 2.1 * ky);
    if (agent->spring != 0 && !(agent->is_killed || agent->ladder_mode || agent->vy != 0)) {
      // pull bubble down when Mugen crouches
      bubble_dst.translate(0.0, 8.0);
    } 
    p.drawImage(bubble_dst, power_up_shield);
  } 

  monitor_csv_save_string(agent->monitor_csv, buffer.str().c_str());
}

static
void paint_audio_seg_map_buf(
  uint8_t* buf, const std::shared_ptr<State>& state, const Agent* agent)
{

  std::shared_ptr<Maze> maze = agent->maze;

  for (int i=0; i<AUDIO_MAP_SIZE; ++i) {
    buf[i] = 0;
  }

  if (agent->power_up_mode) {
    buf[AUDIO_LABEL_MAP.at("power_up_mode")] = 1;
  }
  if (agent->collected_gem) {
    buf[AUDIO_LABEL_MAP.at("gem")] = 1;
  }
  if (agent->is_killed && agent->killed_animation_frame_cnt == DEATH_ANIM_LENGTH) {
    buf[AUDIO_LABEL_MAP.at("killed")] = 1;
  }
  if (agent->killed_monster) {
    buf[AUDIO_LABEL_MAP.at("killed_monster")] = 1;
  }
  if (agent->bumped_head) {
    buf[AUDIO_LABEL_MAP.at("bumped_head")] = 1;
  }
  if (agent->collected_coin) {
    buf[AUDIO_LABEL_MAP.at("coin")] = 1;
  }


  if (agent->ladder_mode && agent->time_alive % 5 == 0) {
    buf[AUDIO_LABEL_MAP.at("ladder_climbing")] = 1;
  } else if (agent->vy == maze->max_jump) {
    buf[AUDIO_LABEL_MAP.at("jump")] = 1;
  } else if (agent->vx != 0 && agent->vy == 0 && agent->spring == 0 && agent->time_alive % 5 == 0) {
    buf[AUDIO_LABEL_MAP.at("walk")] = 1;
  }
}

// -- vecenv --

class VectorOfStates {
public:
  int nenvs;
  int handle;
  QMutex states_mutex;
  std::vector<std::shared_ptr<State>> states; // nenvs
};

static QMutex h2s_mutex;
static QWaitCondition wait_for_actions;
static QWaitCondition wait_for_step_completed;
static std::map<int, std::shared_ptr<VectorOfStates>> h2s;
static std::list<std::shared_ptr<State>> workers_todo;
static int handle_seq = 100;

static std::shared_ptr<VectorOfStates> vstate_find(int handle)
{
  QMutexLocker lock(&h2s_mutex);
  auto f = h2s.find(handle);
  if (f == h2s.end()) {
    fprintf(stderr, "cannot find vstate handle %i\n", handle);
    assert(0);
  }
  return f->second;
}

static
void copy_render_buf(int e, uint8_t* obs_rgb, uint8_t* buf, int res_w, int res_h)
{
  for (int y = 0; y<res_h; y++) {
    for (int x = 0; x<res_w; x++) {
      uint8_t* p = obs_rgb + e*res_h*res_w*3 + y*res_w*3 + x*3;

      p[0] = buf[y*res_w*4 + x*4 + 2];
      p[1] = buf[y*res_w*4 + x*4 + 1];
      p[2] = buf[y*res_w*4 + x*4 + 0];
    }
  }
}

static
void copy_audio_buf(int e, uint8_t* obs_audio, uint8_t* buf, int dim)
{
  for (int x = 0; x<dim; x++) {
    uint8_t* p = obs_audio + e*dim + x;

    p[0] = buf[x];
  }
}

static
void paint_agent_render_buf(uint8_t* buf, int res_w, int res_h, const std::shared_ptr<State>& todo_state, const Agent* a)
{
  QImage img((uchar*)buf, res_w, res_h, res_w * 4, QImage::Format_RGB32);
  QPainter p(&img);
  paint_the_world_for_agent(p, QRect(0, 0, res_w, res_h), todo_state, a);
}

static
void paint_video_data_render_buf(uint8_t* buf, int res_w, int res_h, const std::shared_ptr<State>& todo_state, const Agent* a)
{
  QImage img((uchar*)buf, res_w, res_h, res_w * 4, QImage::Format_RGB32);
  QPainter p(&img);
  paint_the_world_for_video_data(p, QRect(0, 0, res_w, res_h), todo_state, a);
}

static
void stepping_thread(int n)
{
  while (1) {
    std::shared_ptr<State> todo_state;
    std::list<std::shared_ptr<State>> my_todo;
    while (1) {
      if (shutdown_flag)
        return;
      QMutexLocker sleeplock(&h2s_mutex);
      if (workers_todo.empty()) {
        wait_for_actions.wait(&h2s_mutex, 1000); // milliseconds
        continue;
      }
      my_todo.splice(my_todo.begin(), workers_todo, workers_todo.begin());
      break;
    }
    todo_state = my_todo.front();

    {
      QMutexLocker lock(&todo_state->step_mutex);
      assert(todo_state->agent_ready);
      todo_state->step_in_progress = true;
    }

    {
      QMutexLocker lock(&todo_state->state_mutex);
      std::shared_ptr<VectorOfStates> belongs_to = todo_state->belongs_to.lock();
      if (!belongs_to)
        continue;
      Agent& a = todo_state->agent;
      if (a.collect_data && (a.killed_animation_frame_cnt > 1 || a.finished_level_frame_cnt > 1)) {
        // playing out a few frames of the alien being dead
        // we only do this when we are collecting data, not during training
        a.killed_animation_frame_cnt -= 1;
        a.finished_level_frame_cnt -=1;
        if (a.finished_level_frame_cnt > 1) {
          // lets alien fall into the coin at end of level
          // if alien is killed, it is frozen
          a.step();
        }
        paint_video_data_render_buf(a.render_hires_buf, VIDEORES, VIDEORES, todo_state, &a);
        paint_agent_render_buf(a.render_buf, RES_W, RES_H, todo_state, &a);
        paint_audio_seg_map_buf(a.audio_seg_map_buf, todo_state, &a);
      } else {
        todo_state->time += 1;
        bool game_over = todo_state->maze->is_terminated;

        for (const std::shared_ptr<Monster>& m: todo_state->maze->monsters) {
          if (!m->is_dead) {
            m->step(todo_state->maze); // monster steps
            Agent& a = todo_state->agent;
            if (fabs(m->x - a.x) < 0.6 && (a.y - m->y < 1.0) && (a.y - m->y > 0.0) && enemy_themel[m->theme_n].can_be_killed) {
              // monster killed by alien
              m->is_dead = true;
              m->monster_dying_frame_cnt = MONSTER_DEATH_ANIM_LENGTH - 1;
              a.reward += KILL_MONSTER_REWARD;
              a.reward_sum += KILL_MONSTER_REWARD;
              a.killed_monster = true;
            } else if (fabs(m->x - a.x) + fabs(m->y - a.y) < 1.0 && !a.power_up_mode) {
              // agent is killed by monster
              todo_state->maze->is_terminated = true;  // no effect on agent score
              a.is_killed = true;
              a.killed_animation_frame_cnt = DEATH_ANIM_LENGTH;
              a.reward -= DIE_PENALTY;
              a.reward_sum -= DIE_PENALTY;
            }
          }
        }

        if (game_over)
          a.monitor_csv_episode_over();
        a.game_over = game_over;
        if (!a.is_killed) 
          a.step(); // agent steps

        if (game_over) {
          state_reset(todo_state);
        }

        if (a.collect_data) {
          paint_video_data_render_buf(a.render_hires_buf, VIDEORES, VIDEORES, todo_state, &a);
          paint_audio_seg_map_buf(a.audio_seg_map_buf, todo_state, &a);
        }
        paint_agent_render_buf(a.render_buf, RES_W, RES_H, todo_state, &a);
        a.collected_coin = false;
        a.collected_gem = false;
        a.killed_monster = false;
        a.bumped_head = false;
      }
    }

    {
      QMutexLocker lock(&todo_state->step_mutex);
      assert(todo_state->agent_ready);
      assert(todo_state->step_in_progress);
      todo_state->agent_ready = false;
      todo_state->step_in_progress = false;
    }

    wait_for_step_completed.wakeAll();
  }
}

class SteppingThread : public QThread {
public:
  int n;
  SteppingThread(int n)
      : n(n) {}
  void run() { stepping_thread(n); }
};

static std::vector<std::shared_ptr<QThread>> all_threads;

// ------ C Interface ---------

extern "C" {
int get_NUM_ACTIONS()  { return NUM_ACTIONS; }
int get_RES_W()  { return RES_W; }
int get_RES_H()  { return RES_H; }
int get_VIDEORES()  { return VIDEORES; }
int get_AUDIO_MAP_SIZE()  { return AUDIO_MAP_SIZE; }

void initialize_args(int *int_args, float *float_args) {
  NUM_LEVELS = int_args[0];
  PAINT_VEL_INFO = int_args[1] == 1;
  USE_DATA_AUGMENTATION = int_args[2] == 1;
  LEVEL_TIMEOUT = int_args[5];

  AIR_CONTROL = float_args[0];
  BUMP_HEAD_PENALTY = float_args[1];
  DIE_PENALTY = float_args[2];
  KILL_MONSTER_REWARD = float_args[3];
  JUMP_PENALTY = float_args[4];
  SQUAT_PENALTY = float_args[5];
  JITTER_SQUAT_PENALTY = float_args[6];

  int training_sets_seed = int_args[3];
  int rand_seed = int_args[4];

  if (NUM_LEVELS > 0 && (training_sets_seed != -1)) {
    global_rand_gen.seed(training_sets_seed);

    USE_LEVEL_SET = true;

    LEVEL_SEEDS = new int[NUM_LEVELS];

    for (int i = 0; i < NUM_LEVELS; i++) {
      LEVEL_SEEDS[i] = global_rand_gen.randint();
    }
  }

  if (training_sets_seed != -1) {
    global_rand_gen.seed(training_sets_seed);
  } else {
    global_rand_gen.seed(rand_seed);
  }
}

void initialize_set_monitor_dir(const char *d, int monitor_csv_policy_)
{
  monitor_dir = d;
  monitor_csv_policy = monitor_csv_policy_;
}

void init(int threads)
{
  if (bg_images.empty())
    try {
      resource_path = getenv("COINRUN_RESOURCES_PATH");
      if (resource_path == "") {
        throw std::runtime_error("missing environment variable COINRUN_RESOURCES_PATH");
      }
      images_load();
    } catch (const std::exception &e) {
      fprintf(stderr, "ERROR: %s\n", e.what());
      return;
    }

  assert(all_threads.empty());
  all_threads.resize(threads);
  for (int t = 0; t < threads; t++) {
    all_threads[t] = std::shared_ptr<QThread>(new SteppingThread(t));
    all_threads[t]->start();
  }
}

int vec_create(int nenvs, int lump_n, bool collect_data, float default_zoom)
{
  std::shared_ptr<VectorOfStates> vstate(new VectorOfStates);
  vstate->states.resize(nenvs);

  for (int n = 0; n < nenvs; n++) {
    vstate->states[n] = std::shared_ptr<State>(new State(vstate));
    vstate->states[n]->state_n = n;
    if (
        (monitor_csv_policy == 1 && n == 0) ||
        (monitor_csv_policy == 2))
    {
      vstate->states[n]->agent.monitor_csv_open(n + lump_n * nenvs);
    }
    state_reset(vstate->states[n]);
    vstate->states[n]->agent_ready = false;
    vstate->states[n]->agent.zoom = default_zoom;
    vstate->states[n]->agent.target_zoom = default_zoom;
    vstate->states[n]->agent.collect_data = collect_data;
    if (collect_data) {
        vstate->states[n]->agent.render_hires_buf = new uint8_t[VIDEORES*VIDEORES*4];
        vstate->states[n]->agent.audio_seg_map_buf = new uint8_t[AUDIO_MAP_SIZE];
    }

  }
  vstate->nenvs = nenvs;
  int h;
  {
    QMutexLocker lock(&h2s_mutex);
    h = handle_seq++;
    h2s[h] = vstate;
    vstate->handle = h;
  }
  return h;
}

void vec_close(int handle)
{
  if (handle == 0)
    return;
  std::shared_ptr<VectorOfStates> vstate = vstate_find(handle);
  {
    QMutexLocker lock(&h2s_mutex);
    h2s.erase(handle);
  }
}

void vec_step_async_discrete(int handle, int32_t *actions)
{
  std::shared_ptr<VectorOfStates> vstate = vstate_find(handle);
  QMutexLocker sleeplock(&h2s_mutex);
  {
    QMutexLocker lock2(&vstate->states_mutex);
    for (int e = 0; e < vstate->nenvs; e++) {
      std::shared_ptr<State> state = vstate->states[e];
      assert((unsigned int)actions[e] < (unsigned int)NUM_ACTIONS);
      state->agent.action_dx = DISCRETE_ACTIONS[2 * actions[e] + 0];
      state->agent.action_dy = DISCRETE_ACTIONS[2 * actions[e] + 1];
      {
        QMutexLocker lock3(&state->step_mutex);
        state->agent_ready = true;
        workers_todo.push_back(state);
      }
    }
  }
  wait_for_actions.wakeAll();
}

void vec_wait(
  int handle,
  uint8_t* obs_rgb,
  uint8_t* obs_hires_rgb,
  uint8_t* obs_audio_seg_map,
  float* rew,
  bool* done,
  bool* new_level)
{
  std::shared_ptr<VectorOfStates> vstate = vstate_find(handle);
  while (1) {
    QMutexLocker sleeplock(&h2s_mutex);
    bool all_steps_completed = true;
    {
      QMutexLocker lock2(&vstate->states_mutex);
      for (int e = 0; e < vstate->nenvs; e++) {
        std::shared_ptr<State> state = vstate->states[e];
        QMutexLocker lock3(&state->step_mutex);
        all_steps_completed &= !state->agent_ready;
      }
    }
    if (all_steps_completed)
      break;
    wait_for_step_completed.wait(&h2s_mutex, 1000); // milliseconds
  }
  QMutexLocker lock1(&vstate->states_mutex);
  for (int e = 0; e < vstate->nenvs; e++) {
    std::shared_ptr<State> state_e = vstate->states[e];
    QMutexLocker lock2(&state_e->state_mutex);
    // don't really need a mutex, because step is completed, but it's cheap to lock anyway
    Agent& a = state_e->agent;
    if (a.collect_data) {
      copy_render_buf(e, obs_hires_rgb, a.render_hires_buf, VIDEORES, VIDEORES);
      copy_audio_buf(e, obs_audio_seg_map, a.audio_seg_map_buf, AUDIO_MAP_SIZE);
    }
    copy_render_buf(e, obs_rgb, a.render_buf, RES_W, RES_H);
    

    rew[e] = a.reward;
    done[e] = a.game_over;
    new_level[e] = state_e->maze->is_new_level;
    a.reward = 0;
    a.game_over = false;
    state_e->maze->is_new_level = false;
  }
}

void coinrun_shutdown()
{
  shutdown_flag = true;
  while (!all_threads.empty()) {
    std::shared_ptr<QThread> th = all_threads.back();
    all_threads.pop_back();
    th->wait();
    assert(th->isFinished());
  }
}
}

// ------------ GUI -------------

class Viz : public QWidget {
public:
  std::shared_ptr<VectorOfStates> vstate;
  std::shared_ptr<State> show_state;
  int control_handle = -1;

  int font_h;
  int render_mode = 0;

  void paint(QPainter& p, const QRect& rect)
  {
    Agent& agent = show_state->agent;

    paint_the_world_for_video_data(p, rect, show_state, &agent);

    QRect text_rect = rect;
    text_rect.adjust(font_h/3, font_h/3, -font_h/3, -font_h/3);
    p.drawText(text_rect, Qt::AlignRight|Qt::AlignTop, QString::fromStdString(std::to_string(agent.time_alive)));
  }

  void paintEvent(QPaintEvent *ev)
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::HighQualityAntialiasing, true);

    if (render_mode==0) {
      QRect r = rect();
      paint(p, r);

    } else if (render_mode>0) {
      QPixmap bm(render_mode, render_mode);
      {
        QPainter p2(&bm);
        p2.setFont(font());
        paint(p2, QRect(0, 0, render_mode, render_mode));
      }
      p.drawPixmap(rect(), bm);
    }

    if (ffmpeg.isOpen()) {
      QByteArray txt1 = ffmpeg.readAllStandardError();
      if (!txt1.isEmpty())
        fprintf(stderr, "ffmpeg stderr %s\n", txt1.data());
      QImage img(VIDEORES, VIDEORES, QImage::Format_RGB32);
      {
        QPainter p2(&img);
        p2.setFont(font());
        paint(p2, QRect(0, 0, VIDEORES, VIDEORES));
      }
      ffmpeg.write((char*)img.bits(), VIDEORES*VIDEORES*4);
    }
  }

  void set_render_mode(int m)
  {
    render_mode = m;
    if (render_mode==0) {
      choose_font(rect().height());
    }
    if (render_mode>0) {
      choose_font(render_mode);
    }
  }

  QProcess ffmpeg;

  void resizeEvent(QResizeEvent *ev) {
    choose_font(ev->size().height());
    QWidget::resizeEvent(ev);
  }

  void choose_font(int resolution_h) {
    int h = std::min(std::max(resolution_h / 20, 10), 100);
    QFont f("Courier");
    f.setPixelSize(h);
    setFont(f);
    font_h = h;
  }
};

int convert_action(int dx, int dy) {
  if (dy == -1) {
    return NUM_ACTIONS - 1;
  }

  for (int i = 0; i < NUM_ACTIONS; i++) {
    if (dx == DISCRETE_ACTIONS[2 * i] && dy == DISCRETE_ACTIONS[2 * i + 1]) {
      return i;
    }
  }

  assert(false);

  return 0;
}

class TestWindow : public QWidget {
  Q_OBJECT
public:
  QVBoxLayout *vbox;
  Viz *viz;

  TestWindow()
  {
    viz = new Viz();
    vbox = new QVBoxLayout();
    vbox->addWidget(viz, 1);
    setLayout(vbox);
    actions[0] = 0;
    startTimer(66);
  }

  ~TestWindow()
  {
    delete viz;
  }

  void timeout()
  {
    vec_step_async_discrete(viz->control_handle, actions);
    uint8_t bufrgb[RES_W * RES_H * 3];
    float bufrew[1];
    bool bufdone[1];
    vec_wait(viz->control_handle, bufrgb, bufrgb, 0, bufrew, bufdone, bufdone);
    // fprintf(stderr, "%+0.2f %+0.2f %+0.2f\n", bufvel[0], bufvel[1], bufvel[2]);
  }

  int rolling_state_update = 0;

  std::map<int, int> keys_pressed;

  void keyPressEvent(QKeyEvent* kev)
  {
    keys_pressed[kev->key()] = 1;
    if (kev->key() == Qt::Key_Return) {
      viz->show_state->maze->is_terminated = true;
    }
    if (kev->key() == Qt::Key_R) {
  if (viz->ffmpeg.isOpen()) {
    fprintf(stderr, "finishing rec\n");
    viz->ffmpeg.closeWriteChannel();
    viz->ffmpeg.waitForFinished();
    fprintf(stderr, "finished rec\n");
  } else {
    fprintf(stderr, "starting ffmpeg\n");
    QStringList arguments;
    arguments << "-y" << "-r" << "30" <<
      "-f" << "rawvideo" << "-s:v" << (VIDEORES_STR "x" VIDEORES_STR) << "-pix_fmt" << "rgb32" <<
      "-i" << "-" << "-vcodec" << "libx264" << "-pix_fmt" << "yuv420p" << "-crf" << "10" <<
      "coinrun-manualplay.mp4";
    viz->ffmpeg.start("ffmpeg", arguments);
    bool r = viz->ffmpeg.waitForStarted();
    fprintf(stderr, "video rec started %i\n", int(r));
  }
    }
    if (kev->key() == Qt::Key_F1)
      viz->set_render_mode(0);
    if (kev->key() == Qt::Key_F2)
      viz->set_render_mode(64);
    if (kev->key() == Qt::Key_F5)
      viz->show_state->agent.target_zoom = 1.0;
    if (kev->key() == Qt::Key_F6)
      viz->show_state->agent.target_zoom = 2.0;
    if (kev->key() == Qt::Key_F7)
      viz->show_state->agent.target_zoom = 3.0;
    if (kev->key() == Qt::Key_F8)
      viz->show_state->agent.target_zoom = 5.0;

    control();
  }

  void keyReleaseEvent(QKeyEvent *kev)
  {
    keys_pressed[kev->key()] = 0;
    control();
  }

  int actions[1];

  void control()
  {
    if (viz->control_handle <= 0)
      return;
    int dx = keys_pressed[Qt::Key_Right] - keys_pressed[Qt::Key_Left];
    int dy = keys_pressed[Qt::Key_Up] - keys_pressed[Qt::Key_Down];

    actions[0] = convert_action(dx, dy);
  }

  void timerEvent(QTimerEvent *ev)
  {
    timeout();
    update();
    update_window_title();
  }

  void update_window_title()
  {
    setWindowTitle(QString::fromUtf8(
    stdprintf("CoinRun zoom=%0.2f res=%ix%i",
      viz->show_state->agent.zoom,
      viz->render_mode, viz->render_mode
      ).c_str()));
  }
};

extern "C" void test_main_loop()
{
  QApplication *app = 0;
  TestWindow *window = 0;
#ifdef Q_MAC_OS
  [NSApp activateIgnoringOtherApps:YES];
#endif
  {
    static int argc = 1;
    static const char *argv[] = {"CoinRun"};
    app = new QApplication(argc, const_cast<char **>(argv));
  }

  int handle = vec_create(1, 0, false, 5.0);

  window = new TestWindow();
  window->resize(800, 800);

  window->viz->control_handle = handle;
  {
    std::shared_ptr<VectorOfStates> vstate = vstate_find(handle);
    window->viz->vstate = vstate;
    window->viz->show_state = vstate->states[0];
  }
  window->show();

  app->exec();

  delete window;
  delete app;

  vec_close(handle);
  coinrun_shutdown();
}

#include ".generated/coinrun.moc"
