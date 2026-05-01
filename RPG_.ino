#include M5Unified.h
#include Wire.h
#include math.h
#include stdio.h
#include string.h

#if __has_include(M5UnitENV.h)
  #include M5UnitENV.h
  #define HAS_BPS_SENSOR 1
#else
  #define HAS_BPS_SENSOR 0
#endif

static const int SCREEN_W = 128;
static const int SCREEN_H = 128;
static const float WORLD_W = 1152.0f;
static const float WORLD_H = 1152.0f;
static const float VIEW_W = 256.0f;
static const float VIEW_H = 256.0f;
static const float TILE_SIZE = 24.0f;
static const int MAP_W = 48;
static const int MAP_H = 48;
static const int MAX_MOBS = 12;
static const int MAX_NODES = 8;
static const int INVENTORY_SIZE = 6;
static const int UI_STEPS = 6;
static const float THINK_INTERVAL = 0.20f;
static const float CAMP_X = WORLD_W  0.5f;
static const float CAMP_Y = WORLD_H  0.5f;

enum CommandType {
  CMD_IDLE = 0,
  CMD_MOVE,
  CMD_ATTACK,
  CMD_GATHER,
  CMD_RETREAT
};

enum TerrainType {
  TILE_MEADOW = 0,
  TILE_FOREST,
  TILE_STONE,
  TILE_WATER,
  TILE_EMBER,
  TILE_RUIN
};

enum BiomeType {
  BIOME_GREEN = 0,
  BIOME_MARSH,
  BIOME_EMBER,
  BIOME_RUIN
};

enum NodeType {
  NODE_WOOD = 0,
  NODE_ORE
};

enum MobKind {
  MOB_SLIME = 0,
  MOB_WOLF,
  MOB_CRAWLER
};

enum ItemSlot {
  SLOT_WEAPON = 0,
  SLOT_ARMOR,
  SLOT_RING,
  SLOT_COUNT
};

struct Item {
  bool active;
  ItemSlot slot;
  uint8_t rarity;
  int atk;
  int hp;
  int speed;
  char name[28];
};

struct Mob {
  bool used;
  bool alive;
  bool elite;
  MobKind kind;
  float x;
  float y;
  float spawnX;
  float spawnY;
  float hp;
  float maxHp;
  float speed;
  int damage;
  float attackCooldown;
  float respawnTimer;
};

struct ResourceNode {
  bool used;
  bool active;
  float x;
  float y;
  float spawnX;
  float spawnY;
  NodeType type;
  int hitsLeft;
  int maxHits;
  float respawnTimer;
};

struct Hero {
  float x;
  float y;
  float vx;
  float vy;
  float hp;
  float maxHp;
  float mp;
  float maxMp;
  float speed;
  float attackRange;
  float attackCooldown;
  float gatherCooldown;
  int level;
  int xp;
  int xpNeed;
  int gold;
  int wood;
  int ore;
  int baseAtk;
  int totalAtk;
  int gearScore;
  int kills;
  int gathers;
  CommandType command;
  int targetIndex;
  float moveX;
  float moveY;
  float thinkTimer;
};

M5Canvas canvas(&M5.Display);

Hero hero;
Mob mobs[MAX_MOBS];
ResourceNode nodes[MAX_NODES];
Item inventory[INVENTORY_SIZE];
Item equipment[SLOT_COUNT];
uint8_t terrainMap[MAP_H][MAP_W];

unsigned long lastTickMs = 0;
float camX = 0.0f;
float camY = 0.0f;
char brainLine[40] = BOOT;
char brainToken[40] = STREAM READY;
char toastLine[40] = ;
float toastTimer = 0.0f;
int currentLevel = 1;
BiomeType currentBiome = BIOME_GREEN;
uint32_t worldSeed = 0;
bool levelTransitionPending = false;
float levelTransitionTimer = 0.0f;

int brightnessIndex = 0;
int volumeIndex = 0;
bool hardMuted = false;
const uint8_t brightnessLevels[UI_STEPS] = { 220, 180, 136, 96, 64, 32 };
const uint8_t volumeLevels[UI_STEPS] = { 255, 176, 120, 72, 36, 0 };

#if HAS_BPS_SENSOR
QMP6988 qmp;
bool qmpReady = false;
#endif

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8)  8)  ((g & 0xFC)  3)  (b  3);
}

float clampf(float value, float minValue, float maxValue) {
  if (value  minValue) return minValue;
  if (value  maxValue) return maxValue;
  return value;
}

float distSq(float ax, float ay, float bx, float by) {
  const float dx = bx - ax;
  const float dy = by - ay;
  return dx  dx + dy  dy;
}

float dist(float ax, float ay, float bx, float by) {
  return sqrtf(distSq(ax, ay, bx, by));
}

void normalize(float dx, float dy, float &outX, float &outY) {
  const float len = sqrtf(dx  dx + dy  dy);
  if (len  0.0001f) {
    outX = 0.0f;
    outY = 0.0f;
    return;
  }
  outX = dx  len;
  outY = dy  len;
}

uint32_t hash32(uint32_t x) {
  x ^= x  16;
  x = 0x7feb352dU;
  x ^= x  15;
  x = 0x846ca68bU;
  x ^= x  16;
  return x;
}

uint8_t noiseByte(int x, int y, uint32_t seed) {
  const uint32_t mixed = static_castuint32_t(x)  374761393U
                       ^ static_castuint32_t(y)  668265263U
                       ^ seed  2246822519U;
  return static_castuint8_t(hash32(mixed) & 0xFF);
}

float tileNoise(int tx, int ty, uint32_t seed) {
  const float n0 = noiseByte(tx, ty, seed)  255.0f;
  const float n1 = noiseByte(tx  2, ty  2, seed ^ 0xA511E9B3U)  255.0f;
  const float n2 = noiseByte(tx + 5, ty - 3, seed ^ 0x9E3779B9U)  255.0f;
  return n0  0.55f + n1  0.30f + n2  0.15f;
}

void clearItem(Item &item) {
  item.active = false;
  item.slot = SLOT_WEAPON;
  item.rarity = 0;
  item.atk = 0;
  item.hp = 0;
  item.speed = 0;
  item.name[0] = '0';
}

int itemScore(const Item &item) {
  return item.atk  8 + item.hp  2 + item.speed  6;
}

const char slotLabel(ItemSlot slot) {
  switch (slot) {
    case SLOT_WEAPON return WPN;
    case SLOT_ARMOR  return ARM;
    case SLOT_RING   return RNG;
    default          return ---;
  }
}

const char rarityLabel(uint8_t rarity) {
  switch (rarity) {
    case 1 return UNC;
    case 2 return RAR;
    case 3 return EPC;
    default return COM;
  }
}

const char biomeName(BiomeType biome) {
  switch (biome) {
    case BIOME_GREEN return Verdant;
    case BIOME_MARSH return Marsh;
    case BIOME_EMBER return Ember;
    case BIOME_RUIN  return Ruin;
    default          return Wild;
  }
}

uint16_t rarityColor(uint8_t rarity) {
  switch (rarity) {
    case 1 return rgb565(110, 220, 130);
    case 2 return rgb565(100, 180, 255);
    case 3 return rgb565(208, 150, 255);
    default return rgb565(220, 220, 220);
  }
}

void showToast(const char text) {
  strncpy(toastLine, text, sizeof(toastLine) - 1);
  toastLine[sizeof(toastLine) - 1] = '0';
  toastTimer = 1.3f;
}

void applyAudioVideoLevels() {
  M5.Display.setBrightness(brightnessLevels[brightnessIndex]);
  if (hardMuted) {
    M5.Speaker.setVolume(0);
  } else {
    M5.Speaker.setVolume(volumeLevels[volumeIndex]);
  }
}

void playUiTone(uint16_t freq, uint16_t durationMs) {
  if (hardMuted  volumeLevels[volumeIndex] == 0) return;
  M5.Speaker.tone(freq, durationMs);
}

void stepBrightnessAndSoundDown() {
  brightnessIndex = (brightnessIndex + 1) % UI_STEPS;
  volumeIndex = (volumeIndex + 1) % UI_STEPS;
  applyAudioVideoLevels();
  char msg[32];
  snprintf(msg, sizeof(msg), B%03d V%03d, brightnessLevels[brightnessIndex], volumeLevels[volumeIndex]);
  showToast(msg);
  playUiTone(1320 - volumeIndex  90, 18);
}

void toggleFullSound() {
  hardMuted = !hardMuted;
  if (!hardMuted) {
    volumeIndex = 0;
  }
  applyAudioVideoLevels();
  showToast(hardMuted  SOUND OFF  SOUND FULL);
  if (!hardMuted) {
    playUiTone(1660, 28);
  } else {
    M5.Speaker.stop();
  }
}

void recalcHeroStats() {
  hero.maxHp = 170.0f + (hero.level - 1)  18.0f;
  hero.maxMp = 96.0f + (hero.level - 1)  8.0f;
  hero.speed = 120.0f + (hero.level - 1)  1.8f;
  hero.totalAtk = hero.baseAtk + (hero.level - 1)  3;
  hero.gearScore = 0;

  for (int i = 0; i  SLOT_COUNT; ++i) {
    if (!equipment[i].active) continue;
    hero.totalAtk += equipment[i].atk;
    hero.maxHp += equipment[i].hp;
    hero.speed += equipment[i].speed;
    hero.gearScore += itemScore(equipment[i]);
  }

  hero.hp = clampf(hero.hp, 0.0f, hero.maxHp);
  hero.mp = clampf(hero.mp, 0.0f, hero.maxMp);
}

int usedInventorySlots() {
  int count = 0;
  for (int i = 0; i  INVENTORY_SIZE; ++i) {
    if (inventory[i].active) count++;
  }
  return count;
}

int firstFreeInventorySlot() {
  for (int i = 0; i  INVENTORY_SIZE; ++i) {
    if (!inventory[i].active) return i;
  }
  return -1;
}

bool addItemToInventory(const Item &item) {
  const int slot = firstFreeInventorySlot();
  if (slot  0) {
    hero.gold += max(6, itemScore(item)  5);
    showToast(BAG FULL - GOLD);
    playUiTone(220, 28);
    return false;
  }
  inventory[slot] = item;
  playUiTone(1420 + item.rarity  140, 24);
  return true;
}

void equipInventoryItem(int inventoryIndex) {
  if (inventoryIndex  0  inventoryIndex = INVENTORY_SIZE) return;
  if (!inventory[inventoryIndex].active) return;

  const ItemSlot slot = inventory[inventoryIndex].slot;
  Item temp = equipment[slot];
  equipment[slot] = inventory[inventoryIndex];

  if (temp.active) {
    inventory[inventoryIndex] = temp;
  } else {
    clearItem(inventory[inventoryIndex]);
  }

  recalcHeroStats();
}

void autoEquipBest() {
  for (int i = 0; i  INVENTORY_SIZE; ++i) {
    if (!inventory[i].active) continue;
    const ItemSlot slot = inventory[i].slot;
    const int currentScore = equipment[slot].active  itemScore(equipment[slot])  0;
    if (itemScore(inventory[i])  currentScore + 4) {
      equipInventoryItem(i);
      snprintf(brainToken, sizeof(brainToken), AUTO EQUIP %s, slotLabel(slot));
      showToast(GEAR UPGRADE);
      playUiTone(1560, 24);
    }
  }
}

Item rollLootItem(bool eliteMob) {
  Item item;
  clearItem(item);
  item.active = true;
  item.slot = static_castItemSlot(random(0, SLOT_COUNT));

  const int roll = random(0, 100) + (eliteMob  18  0) + hero.level  2;
  if (roll  108) item.rarity = 3;
  else if (roll  78) item.rarity = 2;
  else if (roll  48) item.rarity = 1;
  else item.rarity = 0;

  const int mul = 8 + item.rarity  4 + currentLevel  2;
  switch (item.slot) {
    case SLOT_WEAPON
      item.atk = 2 + mul  3;
      item.hp = 0;
      item.speed = item.rarity = 2  1  0;
      break;
    case SLOT_ARMOR
      item.atk = 0;
      item.hp = 10 + mul  2;
      item.speed = 0;
      break;
    case SLOT_RING
      item.atk = 1 + mul  5;
      item.hp = 4 + mul;
      item.speed = 1 + item.rarity;
      break;
    default
      break;
  }

  const char prefix = Scout;
  switch (random(0, 6)) {
    case 0 prefix = Scout; break;
    case 1 prefix = Ash; break;
    case 2 prefix = Fen; break;
    case 3 prefix = Iron; break;
    case 4 prefix = Rune; break;
    case 5 prefix = Void; break;
  }

  const char noun = Blade;
  switch (item.slot) {
    case SLOT_WEAPON noun = Blade; break;
    case SLOT_ARMOR  noun = Mail; break;
    case SLOT_RING   noun = Ring; break;
    default break;
  }

  snprintf(item.name, sizeof(item.name), %s %s, prefix, noun);
  return item;
}

void gainXp(int amount) {
  hero.xp += amount;
  while (hero.xp = hero.xpNeed) {
    hero.xp -= hero.xpNeed;
    hero.level++;
    hero.xpNeed = hero.xpNeed + 20 + hero.level  10;
    recalcHeroStats();
    hero.hp = hero.maxHp;
    hero.mp = hero.maxMp;
    snprintf(brainToken, sizeof(brainToken), LEVEL %d ONLINE, hero.level);
    showToast(LEVEL UP);
    playUiTone(1760, 36);
  }
}

uint32_t sensorSeedSalt() {
#if HAS_BPS_SENSOR
  if (qmpReady && qmp.update()) {
    const uint32_t p = static_castuint32_t(qmp.pressure);
    const uint32_t t = static_castuint32_t((qmp.cTemp + 40.0f)  100.0f);
    return p ^ (t  8);
  }
#endif
  return 0;
}

uint8_t terrainAtTile(int tx, int ty) {
  tx = max(0, min(MAP_W - 1, tx));
  ty = max(0, min(MAP_H - 1, ty));
  return terrainMap[ty][tx];
}

uint8_t terrainAtWorld(float wx, float wy) {
  int tx = static_castint(wx  TILE_SIZE);
  int ty = static_castint(wy  TILE_SIZE);
  return terrainAtTile(tx, ty);
}

float terrainSpeedFactor(uint8_t terrain) {
  switch (terrain) {
    case TILE_WATER return 0.72f;
    case TILE_STONE return 0.88f;
    case TILE_RUIN  return 0.90f;
    default         return 1.00f;
  }
}

void generateTerrainMap() {
  for (int ty = 0; ty  MAP_H; ++ty) {
    for (int tx = 0; tx  MAP_W; ++tx) {
      const float n = tileNoise(tx, ty, worldSeed);
      uint8_t tile = TILE_MEADOW;

      switch (currentBiome) {
        case BIOME_GREEN
          if (n  0.14f) tile = TILE_WATER;
          else if (n  0.42f) tile = TILE_MEADOW;
          else if (n  0.74f) tile = TILE_FOREST;
          else tile = TILE_STONE;
          break;
        case BIOME_MARSH
          if (n  0.24f) tile = TILE_WATER;
          else if (n  0.52f) tile = TILE_FOREST;
          else if (n  0.76f) tile = TILE_MEADOW;
          else tile = TILE_STONE;
          break;
        case BIOME_EMBER
          if (n  0.15f) tile = TILE_STONE;
          else if (n  0.48f) tile = TILE_EMBER;
          else if (n  0.75f) tile = TILE_MEADOW;
          else tile = TILE_FOREST;
          break;
        case BIOME_RUIN
          if (n  0.16f) tile = TILE_WATER;
          else if (n  0.46f) tile = TILE_RUIN;
          else if (n  0.72f) tile = TILE_STONE;
          else tile = TILE_FOREST;
          break;
      }

      const float centerDist = dist(tx  TILE_SIZE + TILE_SIZE  0.5f,
                                    ty  TILE_SIZE + TILE_SIZE  0.5f,
                                    CAMP_X, CAMP_Y);
      if (centerDist  96.0f) {
        tile = TILE_MEADOW;
      }
      terrainMap[ty][tx] = tile;
    }
  }
}

bool randomSpawnPoint(float minCampDist, float &outX, float &outY) {
  for (int attempt = 0; attempt  80; ++attempt) {
    const int tx = random(1, MAP_W - 1);
    const int ty = random(1, MAP_H - 1);
    const float wx = tx  TILE_SIZE + TILE_SIZE  0.5f;
    const float wy = ty  TILE_SIZE + TILE_SIZE  0.5f;
    if (dist(wx, wy, CAMP_X, CAMP_Y)  minCampDist) continue;
    if (terrainAtTile(tx, ty) == TILE_WATER) continue;
    outX = wx;
    outY = wy;
    return true;
  }
  outX = CAMP_X + random(-180, 181);
  outY = CAMP_Y + random(-180, 181);
  return true;
}

void setMobDefaults(Mob &mob) {
  mob.used = false;
  mob.alive = false;
  mob.elite = false;
  mob.kind = MOB_SLIME;
  mob.x = 0.0f;
  mob.y = 0.0f;
  mob.spawnX = 0.0f;
  mob.spawnY = 0.0f;
  mob.hp = 0.0f;
  mob.maxHp = 0.0f;
  mob.speed = 0.0f;
  mob.damage = 0;
  mob.attackCooldown = 0.0f;
  mob.respawnTimer = 0.0f;
}

void setNodeDefaults(ResourceNode &node) {
  node.used = false;
  node.active = false;
  node.x = 0.0f;
  node.y = 0.0f;
  node.spawnX = 0.0f;
  node.spawnY = 0.0f;
  node.type = NODE_WOOD;
  node.hitsLeft = 0;
  node.maxHits = 0;
  node.respawnTimer = 0.0f;
}

void generateLevel(int level) {
  currentLevel = level;
  currentBiome = static_castBiomeType((random(0, 1000) + level) % 4);
  worldSeed = (static_castuint32_t(random(1, 0x7FFF))  8) ^ sensorSeedSalt() ^ (static_castuint32_t(level)  2654435761U);
  generateTerrainMap();

  for (int i = 0; i  MAX_MOBS; ++i) {
    setMobDefaults(mobs[i]);
  }
  for (int i = 0; i  MAX_NODES; ++i) {
    setNodeDefaults(nodes[i]);
  }

  const int mobCount = min(MAX_MOBS, 5 + level  2);
  for (int i = 0; i  mobCount; ++i) {
    Mob &mob = mobs[i];
    mob.used = true;
    mob.alive = true;
    mob.elite = (i == mobCount - 1);
    mob.kind = static_castMobKind((i + level + currentBiome) % 3);
    randomSpawnPoint(mob.elite  220.0f  140.0f, mob.spawnX, mob.spawnY);
    mob.x = mob.spawnX;
    mob.y = mob.spawnY;
    mob.maxHp = mob.elite  120.0f + level  22.0f  42.0f + level  9.0f + (i % 3)  6.0f;
    mob.hp = mob.maxHp;
    mob.speed = mob.elite  62.0f  48.0f + (mob.kind  6.0f);
    mob.damage = mob.elite  16 + level  2  5 + level + (i % 3);
    mob.attackCooldown = 0.0f;
    mob.respawnTimer = 0.0f;
  }

  const int nodeCount = min(MAX_NODES, 4 + level);
  for (int i = 0; i  nodeCount; ++i) {
    ResourceNode &node = nodes[i];
    node.used = true;
    node.active = true;
    node.type = (i % 2 == 0)  NODE_WOOD  NODE_ORE;
    randomSpawnPoint(110.0f, node.spawnX, node.spawnY);
    node.x = node.spawnX;
    node.y = node.spawnY;
    node.maxHits = node.type == NODE_WOOD  3 + (level  3)  4 + (level  3);
    node.hitsLeft = node.maxHits;
    node.respawnTimer = 0.0f;
  }

  hero.x = CAMP_X;
  hero.y = CAMP_Y;
  hero.vx = 0.0f;
  hero.vy = 0.0f;
  hero.command = CMD_IDLE;
  hero.targetIndex = -1;
  hero.moveX = CAMP_X;
  hero.moveY = CAMP_Y;
  hero.thinkTimer = 0.08f;
  hero.hp = hero.maxHp;
  hero.mp = hero.maxMp;

  levelTransitionPending = false;
  levelTransitionTimer = 0.0f;
  snprintf(brainLine, sizeof(brainLine), LEVEL %d %s, currentLevel, biomeName(currentBiome));
  snprintf(brainToken, sizeof(brainToken), PROCGEN ONLINE);
  showToast(NEW LEVEL);
  playUiTone(1120 + currentLevel  20, 28);
}

void initWorld() {
  for (int i = 0; i  INVENTORY_SIZE; ++i) {
    clearItem(inventory[i]);
  }
  for (int i = 0; i  SLOT_COUNT; ++i) {
    clearItem(equipment[i]);
  }

  hero.x = CAMP_X;
  hero.y = CAMP_Y;
  hero.vx = 0.0f;
  hero.vy = 0.0f;
  hero.level = 1;
  hero.xp = 0;
  hero.xpNeed = 56;
  hero.gold = 0;
  hero.wood = 0;
  hero.ore = 0;
  hero.baseAtk = 11;
  hero.attackRange = 24.0f;
  hero.attackCooldown = 0.0f;
  hero.gatherCooldown = 0.0f;
  hero.command = CMD_IDLE;
  hero.targetIndex = -1;
  hero.moveX = CAMP_X;
  hero.moveY = CAMP_Y;
  hero.thinkTimer = THINK_INTERVAL;
  hero.kills = 0;
  hero.gathers = 0;
  hero.hp = 170.0f;
  hero.mp = 96.0f;
  recalcHeroStats();
  hero.hp = hero.maxHp;
  hero.mp = hero.maxMp;

  equipment[SLOT_WEAPON].active = true;
  equipment[SLOT_WEAPON].slot = SLOT_WEAPON;
  equipment[SLOT_WEAPON].rarity = 0;
  equipment[SLOT_WEAPON].atk = 3;
  equipment[SLOT_WEAPON].hp = 0;
  equipment[SLOT_WEAPON].speed = 0;
  strncpy(equipment[SLOT_WEAPON].name, Rust Blade, sizeof(equipment[SLOT_WEAPON].name) - 1);
  equipment[SLOT_WEAPON].name[sizeof(equipment[SLOT_WEAPON].name) - 1] = '0';

  equipment[SLOT_ARMOR].active = true;
  equipment[SLOT_ARMOR].slot = SLOT_ARMOR;
  equipment[SLOT_ARMOR].rarity = 0;
  equipment[SLOT_ARMOR].atk = 0;
  equipment[SLOT_ARMOR].hp = 16;
  equipment[SLOT_ARMOR].speed = 0;
  strncpy(equipment[SLOT_ARMOR].name, Field Mail, sizeof(equipment[SLOT_ARMOR].name) - 1);
  equipment[SLOT_ARMOR].name[sizeof(equipment[SLOT_ARMOR].name) - 1] = '0';

  clearItem(equipment[SLOT_RING]);
  recalcHeroStats();
  hero.hp = hero.maxHp;
  hero.mp = hero.maxMp;

  generateLevel(1);
}

int countLivingMobs() {
  int count = 0;
  for (int i = 0; i  MAX_MOBS; ++i) {
    if (mobs[i].used && mobs[i].alive) count++;
  }
  return count;
}

int nearestLivingMob(float maxDistance) {
  int bestIndex = -1;
  float bestDist = maxDistance;
  for (int i = 0; i  MAX_MOBS; ++i) {
    if (!mobs[i].used  !mobs[i].alive) continue;
    const float d = dist(hero.x, hero.y, mobs[i].x, mobs[i].y);
    if (d  bestDist) {
      bestDist = d;
      bestIndex = i;
    }
  }
  return bestIndex;
}

int nearestActiveNode(float maxDistance) {
  int bestIndex = -1;
  float bestDist = maxDistance;
  for (int i = 0; i  MAX_NODES; ++i) {
    if (!nodes[i].used  !nodes[i].active) continue;
    const float d = dist(hero.x, hero.y, nodes[i].x, nodes[i].y);
    if (d  bestDist) {
      bestDist = d;
      bestIndex = i;
    }
  }
  return bestIndex;
}

void scheduleNextLevel() {
  if (levelTransitionPending) return;
  levelTransitionPending = true;
  levelTransitionTimer = 1.35f;
  hero.command = CMD_IDLE;
  hero.targetIndex = -1;
  snprintf(brainLine, sizeof(brainLine), LEVEL CLEAR);
  snprintf(brainToken, sizeof(brainToken), WARPING FORWARD);
  showToast(ELITE DOWN);
  playUiTone(1920, 46);
}

void killMob(int index) {
  if (index  0  index = MAX_MOBS) return;
  Mob &mob = mobs[index];
  mob.alive = false;
  mob.respawnTimer = mob.elite  14.0f  8.0f;
  hero.gold += mob.elite  22  9 + currentLevel;
  hero.kills++;
  gainXp(mob.elite  40 + currentLevel  4  14 + currentLevel  3);
  playUiTone(mob.elite  980  760, mob.elite  42  22);

  if (random(0, 100)  (mob.elite  92  36)) {
    Item loot = rollLootItem(mob.elite);
    addItemToInventory(loot);
  }

  if (mob.elite  countLivingMobs() == 0) {
    scheduleNextLevel();
  }
}

void microGPTThink() {
  autoEquipBest();

  if (levelTransitionPending) {
    hero.command = CMD_IDLE;
    snprintf(brainLine, sizeof(brainLine), WARP CHARGE);
    snprintf(brainToken, sizeof(brainToken), NEXT LEVEL);
    return;
  }

  const float hpRatio = hero.hp  hero.maxHp;
  const int nearMob = nearestLivingMob(240.0f);
  const int farMob = nearestLivingMob(520.0f);
  const int nearNode = nearestActiveNode(220.0f);
  const int farNode = nearestActiveNode(460.0f);

  if (hpRatio  0.28f) {
    hero.command = CMD_RETREAT;
    hero.targetIndex = -1;
    hero.moveX = CAMP_X;
    hero.moveY = CAMP_Y;
    snprintf(brainLine, sizeof(brainLine), RETREAT CAMP);
    snprintf(brainToken, sizeof(brainToken), HP LOW %.0f%%, hpRatio  100.0f);
    return;
  }

  if (nearMob = 0) {
    hero.command = CMD_ATTACK;
    hero.targetIndex = nearMob;
    snprintf(brainLine, sizeof(brainLine), ENGAGE HOSTILE);
    snprintf(brainToken, sizeof(brainToken), mobs[nearMob].elite  ELITE PRIORITY  PACK SWEEP);
    return;
  }

  if (nearNode = 0 && usedInventorySlots()  INVENTORY_SIZE) {
    hero.command = CMD_GATHER;
    hero.targetIndex = nearNode;
    snprintf(brainLine, sizeof(brainLine), RESOURCE PATH);
    snprintf(brainToken, sizeof(brainToken), nodes[nearNode].type == NODE_WOOD  WOOD FARM  ORE FARM);
    return;
  }

  if (farMob = 0) {
    hero.command = CMD_ATTACK;
    hero.targetIndex = farMob;
    snprintf(brainLine, sizeof(brainLine), CHASE TARGET);
    snprintf(brainToken, sizeof(brainToken), AUTOHUNT LOOP);
    return;
  }

  if (farNode = 0 && usedInventorySlots()  INVENTORY_SIZE) {
    hero.command = CMD_GATHER;
    hero.targetIndex = farNode;
    snprintf(brainLine, sizeof(brainLine), SWING TO NODE);
    snprintf(brainToken, sizeof(brainToken), PROC RESOURCE);
    return;
  }

  hero.command = CMD_MOVE;
  hero.targetIndex = -1;
  const float t = millis()  0.0011f + currentLevel  0.45f;
  hero.moveX = CAMP_X + sinf(t)  170.0f + cosf(t  0.7f)  60.0f;
  hero.moveY = CAMP_Y + cosf(t  0.92f)  150.0f;
  hero.moveX = clampf(hero.moveX, 32.0f, WORLD_W - 32.0f);
  hero.moveY = clampf(hero.moveY, 32.0f, WORLD_H - 32.0f);
  snprintf(brainLine, sizeof(brainLine), PATROL %s, biomeName(currentBiome));
  snprintf(brainToken, sizeof(brainToken), SEED %lu, static_castunsigned long(worldSeed & 0xFFFFU));
}

void updateHero(float dt) {
  hero.attackCooldown = max(0.0f, hero.attackCooldown - dt);
  hero.gatherCooldown = max(0.0f, hero.gatherCooldown - dt);
  hero.thinkTimer -= dt;
  toastTimer = max(0.0f, toastTimer - dt);

  if (levelTransitionPending) {
    hero.vx = 0.8f;
    hero.vy = 0.8f;
    hero.x += hero.vx  dt;
    hero.y += hero.vy  dt;
    levelTransitionTimer -= dt;
    if (levelTransitionTimer = 0.0f) {
      generateLevel(currentLevel + 1);
    }
    return;
  }

  if (hero.thinkTimer = 0.0f) {
    hero.thinkTimer = THINK_INTERVAL;
    microGPTThink();
  }

  float targetX = hero.x;
  float targetY = hero.y;
  bool hasMove = false;

  if (hero.command == CMD_RETREAT  hero.command == CMD_MOVE) {
    targetX = hero.moveX;
    targetY = hero.moveY;
    hasMove = true;
  } else if (hero.command == CMD_ATTACK && hero.targetIndex = 0 && mobs[hero.targetIndex].used && mobs[hero.targetIndex].alive) {
    targetX = mobs[hero.targetIndex].x;
    targetY = mobs[hero.targetIndex].y;
    const float d = dist(hero.x, hero.y, targetX, targetY);
    if (d = hero.attackRange) {
      if (hero.attackCooldown = 0.0f) {
        hero.attackCooldown = 0.44f;
        mobs[hero.targetIndex].hp -= hero.totalAtk;
        hero.mp = clampf(hero.mp + 2.0f, 0.0f, hero.maxMp);
        if (mobs[hero.targetIndex].hp = 0.0f) {
          killMob(hero.targetIndex);
          hero.command = CMD_IDLE;
          hero.targetIndex = -1;
        }
      }
    } else {
      hasMove = true;
    }
  } else if (hero.command == CMD_GATHER && hero.targetIndex = 0 && nodes[hero.targetIndex].used && nodes[hero.targetIndex].active) {
    targetX = nodes[hero.targetIndex].x;
    targetY = nodes[hero.targetIndex].y;
    const float d = dist(hero.x, hero.y, targetX, targetY);
    if (d = 18.0f) {
      if (hero.gatherCooldown = 0.0f) {
        hero.gatherCooldown = 0.58f;
        nodes[hero.targetIndex].hitsLeft--;
        if (nodes[hero.targetIndex].type == NODE_WOOD) hero.wood += 2;
        else hero.ore += 2;
        hero.gathers++;
        gainXp(5 + currentLevel);
        playUiTone(nodes[hero.targetIndex].type == NODE_WOOD  620  840, 16);
        if (nodes[hero.targetIndex].hitsLeft = 0) {
          nodes[hero.targetIndex].active = false;
          nodes[hero.targetIndex].respawnTimer = 9.0f;
          hero.command = CMD_IDLE;
          hero.targetIndex = -1;
        }
      }
    } else {
      hasMove = true;
    }
  } else {
    hero.command = CMD_IDLE;
    hero.targetIndex = -1;
  }

  const float moveFactor = terrainSpeedFactor(terrainAtWorld(hero.x, hero.y));
  if (hasMove) {
    float nx = 0.0f;
    float ny = 0.0f;
    normalize(targetX - hero.x, targetY - hero.y, nx, ny);
    hero.vx = nx  hero.speed  moveFactor;
    hero.vy = ny  hero.speed  moveFactor;
  } else {
    hero.vx = 0.70f;
    hero.vy = 0.70f;
  }

  hero.x = clampf(hero.x + hero.vx  dt, 20.0f, WORLD_W - 20.0f);
  hero.y = clampf(hero.y + hero.vy  dt, 20.0f, WORLD_H - 20.0f);

  const float campDist = dist(hero.x, hero.y, CAMP_X, CAMP_Y);
  if (campDist  58.0f) {
    hero.hp = clampf(hero.hp + 16.0f  dt, 0.0f, hero.maxHp);
    hero.mp = clampf(hero.mp + 22.0f  dt, 0.0f, hero.maxMp);
    if (hero.command == CMD_RETREAT && hero.hp  hero.maxHp  0.86f) {
      hero.command = CMD_IDLE;
      hero.targetIndex = -1;
    }
  } else {
    hero.mp = clampf(hero.mp + 5.0f  dt, 0.0f, hero.maxMp);
  }
}

void updateMobs(float dt) {
  for (int i = 0; i  MAX_MOBS; ++i) {
    Mob &mob = mobs[i];
    if (!mob.used) continue;

    mob.attackCooldown = max(0.0f, mob.attackCooldown - dt);

    if (!mob.alive) {
      mob.respawnTimer -= dt;
      if (mob.respawnTimer = 0.0f && !levelTransitionPending) {
        mob.alive = true;
        mob.hp = mob.maxHp;
        mob.x = mob.spawnX;
        mob.y = mob.spawnY;
      }
      continue;
    }

    const float dHero = dist(mob.x, mob.y, hero.x, hero.y);
    if (dHero  132.0f) {
      float nx = 0.0f;
      float ny = 0.0f;
      normalize(hero.x - mob.x, hero.y - mob.y, nx, ny);
      const float moveFactor = terrainSpeedFactor(terrainAtWorld(mob.x, mob.y));
      mob.x += nx  mob.speed  moveFactor  dt;
      mob.y += ny  mob.speed  moveFactor  dt;

      if (dHero  18.0f && mob.attackCooldown = 0.0f) {
        mob.attackCooldown = mob.elite  0.70f  1.00f;
        hero.hp = max(0.0f, hero.hp - static_castfloat(mob.damage));
        if (hero.hp = 0.0f) {
          hero.hp = hero.maxHp;
          hero.mp = hero.maxMp;
          hero.x = CAMP_X;
          hero.y = CAMP_Y;
          hero.command = CMD_RETREAT;
          hero.targetIndex = -1;
          snprintf(brainLine, sizeof(brainLine), RESPAWN CAMP);
          snprintf(brainToken, sizeof(brainToken), RESET ROUTE);
          showToast(RESPAWN);
          playUiTone(320, 48);
        }
      }
    } else {
      const float dSpawn = dist(mob.x, mob.y, mob.spawnX, mob.spawnY);
      if (dSpawn  8.0f) {
        float nx = 0.0f;
        float ny = 0.0f;
        normalize(mob.spawnX - mob.x, mob.spawnY - mob.y, nx, ny);
        const float moveFactor = terrainSpeedFactor(terrainAtWorld(mob.x, mob.y));
        mob.x += nx  mob.speed  0.60f  moveFactor  dt;
        mob.y += ny  mob.speed  0.60f  moveFactor  dt;
      }
    }
  }
}

void updateNodes(float dt) {
  for (int i = 0; i  MAX_NODES; ++i) {
    ResourceNode &node = nodes[i];
    if (!node.used) continue;
    if (!node.active) {
      node.respawnTimer -= dt;
      if (node.respawnTimer = 0.0f && !levelTransitionPending) {
        node.active = true;
        node.hitsLeft = node.maxHits;
      }
    }
  }
}

void updateCamera() {
  camX = clampf(hero.x - VIEW_W  0.5f, 0.0f, WORLD_W - VIEW_W);
  camY = clampf(hero.y - VIEW_H  0.5f, 0.0f, WORLD_H - VIEW_H);
}

int toScreenX(float worldX) {
  return static_castint((worldX - camX)  (static_castfloat(SCREEN_W)  VIEW_W));
}

int toScreenY(float worldY) {
  return static_castint((worldY - camY)  (static_castfloat(SCREEN_H)  VIEW_H));
}

int tilePixels() {
  return static_castint(TILE_SIZE  (static_castfloat(SCREEN_W)  VIEW_W)) + 1;
}

void drawBar(int x, int y, int w, int h, float ratio, uint16_t fillColor, uint16_t backColor) {
  ratio = clampf(ratio, 0.0f, 1.0f);
  canvas.fillRoundRect(x, y, w, h, 2, backColor);
  const int fillW = static_castint(w  ratio);
  if (fillW  0) {
    canvas.fillRoundRect(x, y, fillW, h, 2, fillColor);
  }
}

void drawTerrainTile(int sx, int sy, int size, uint8_t tile) {
  uint16_t base = rgb565(40, 100, 52);
  uint16_t dark = rgb565(24, 72, 36);
  uint16_t light = rgb565(78, 150, 88);

  switch (tile) {
    case TILE_MEADOW
      base = rgb565(44, 116, 60);
      dark = rgb565(24, 82, 40);
      light = rgb565(82, 162, 94);
      break;
    case TILE_FOREST
      base = rgb565(28, 84, 46);
      dark = rgb565(16, 54, 28);
      light = rgb565(52, 120, 68);
      break;
    case TILE_STONE
      base = rgb565(72, 82, 92);
      dark = rgb565(44, 50, 60);
      light = rgb565(114, 124, 136);
      break;
    case TILE_WATER
      base = rgb565(28, 92, 140);
      dark = rgb565(18, 54, 98);
      light = rgb565(94, 174, 220);
      break;
    case TILE_EMBER
      base = rgb565(88, 44, 34);
      dark = rgb565(54, 22, 18);
      light = rgb565(188, 100, 58);
      break;
    case TILE_RUIN
      base = rgb565(88, 78, 108);
      dark = rgb565(52, 42, 68);
      light = rgb565(138, 120, 166);
      break;
  }

  canvas.fillRect(sx, sy, size, size, base);
  const int p = max(1, size  5);
  canvas.fillRect(sx + p, sy + p, p, p, dark);
  canvas.fillRect(sx + size - 2  p, sy + p, p, p, light);
  canvas.fillRect(sx + p, sy + size - 2  p, p, p, light);

  if (tile == TILE_FOREST) {
    canvas.fillRect(sx + size  2 - p, sy + size  2 - p, p  2, p  2, dark);
  } else if (tile == TILE_WATER) {
    canvas.drawFastHLine(sx + p, sy + size  2, size - p  2, light);
  } else if (tile == TILE_EMBER) {
    canvas.fillRect(sx + size  2, sy + p, p, size - p  2, light);
  } else if (tile == TILE_RUIN) {
    canvas.drawRect(sx + p, sy + p, size - p  2, size - p  2, dark);
  }
}

void drawTerrain() {
  const int size = tilePixels();
  const int tx0 = max(0, static_castint(camX  TILE_SIZE) - 1);
  const int ty0 = max(0, static_castint(camY  TILE_SIZE) - 1);
  const int tx1 = min(MAP_W - 1, static_castint((camX + VIEW_W)  TILE_SIZE) + 1);
  const int ty1 = min(MAP_H - 1, static_castint((camY + VIEW_H)  TILE_SIZE) + 1);

  for (int ty = ty0; ty = ty1; ++ty) {
    for (int tx = tx0; tx = tx1; ++tx) {
      const int sx = toScreenX(tx  TILE_SIZE);
      const int sy = toScreenY(ty  TILE_SIZE);
      drawTerrainTile(sx, sy, size, terrainMap[ty][tx]);
    }
  }
}

void drawCampSprite(int sx, int sy) {
  canvas.fillCircle(sx, sy + 2, 11, rgb565(56, 40, 24));
  canvas.drawCircle(sx, sy + 2, 13, rgb565(255, 196, 112));
  canvas.fillRect(sx - 5, sy + 1, 10, 2, rgb565(92, 58, 32));
  canvas.fillRect(sx - 2, sy - 4, 4, 7, rgb565(255, 146, 66));
  canvas.fillRect(sx - 1, sy - 7, 2, 4, rgb565(255, 225, 146));
}

void drawHeroSprite(int sx, int sy) {
  canvas.fillCircle(sx, sy + 4, 7, rgb565(14, 18, 24));
  canvas.fillRect(sx - 1, sy - 7, 2, 2, rgb565(160, 92, 50));
  canvas.fillRect(sx - 2, sy - 5, 4, 3, rgb565(255, 212, 132));
  canvas.fillRect(sx - 4, sy - 1, 8, 6, rgb565(255, 182, 88));
  canvas.fillRect(sx - 5, sy + 1, 10, 2, rgb565(255, 232, 170));
  canvas.fillRect(sx - 3, sy + 5, 2, 4, rgb565(30, 46, 76));
  canvas.fillRect(sx + 1, sy + 5, 2, 4, rgb565(30, 46, 76));
  canvas.fillRect(sx + 5, sy - 4, 2, 10, rgb565(190, 236, 255));
  canvas.drawPixel(sx + 6, sy - 5, rgb565(255, 255, 255));
}

void drawMobSprite(int sx, int sy, const Mob &mob) {
  uint16_t body = rgb565(92, 236, 208);
  uint16_t dark = rgb565(26, 80, 70);
  uint16_t eye = rgb565(255, 246, 210);

  if (mob.elite) {
    body = rgb565(194, 118, 255);
    dark = rgb565(82, 34, 120);
  } else if (mob.kind == MOB_WOLF) {
    body = rgb565(108, 198, 255);
    dark = rgb565(44, 92, 134);
  } else if (mob.kind == MOB_CRAWLER) {
    body = rgb565(126, 234, 132);
    dark = rgb565(44, 96, 52);
  }

  canvas.fillCircle(sx, sy + 3, mob.elite  7  6, rgb565(10, 18, 24));
  if (mob.kind == MOB_WOLF) {
    canvas.fillRect(sx - 5, sy - 3, 10, 6, body);
    canvas.fillRect(sx + 3, sy - 5, 4, 4, body);
    canvas.fillRect(sx - 5, sy + 3, 2, 4, dark);
    canvas.fillRect(sx + 3, sy + 3, 2, 4, dark);
    canvas.drawPixel(sx + 4, sy - 3, eye);
  } else if (mob.kind == MOB_CRAWLER) {
    canvas.fillRect(sx - 4, sy - 4, 8, 8, body);
    canvas.fillRect(sx - 6, sy + 2, 2, 2, dark);
    canvas.fillRect(sx + 4, sy + 2, 2, 2, dark);
    canvas.drawPixel(sx - 1, sy - 1, eye);
    canvas.drawPixel(sx + 1, sy - 1, eye);
  } else {
    canvas.fillRect(sx - 5, sy - 4, 10, 8, body);
    canvas.fillRect(sx - 4, sy + 4, 8, 2, dark);
    canvas.drawPixel(sx - 2, sy - 1, eye);
    canvas.drawPixel(sx + 2, sy - 1, eye);
  }

  const float hpRatio = mob.hp  mob.maxHp;
  drawBar(sx - 7, sy - 11, 14, 2, hpRatio, rgb565(255, 92, 92), rgb565(40, 20, 20));
}

void drawNodeSprite(int sx, int sy, const ResourceNode &node) {
  if (node.type == NODE_WOOD) {
    canvas.fillRect(sx - 1, sy + 1, 2, 6, rgb565(96, 58, 28));
    canvas.fillRect(sx - 5, sy - 5, 10, 6, rgb565(68, 166, 88));
    canvas.fillRect(sx - 3, sy - 7, 6, 3, rgb565(98, 206, 118));
  } else {
    canvas.fillRect(sx - 1, sy + 2, 2, 5, rgb565(40, 80, 110));
    canvas.fillTriangle(sx, sy - 7, sx - 5, sy + 2, sx + 5, sy + 2, rgb565(112, 220, 255));
    canvas.drawFastVLine(sx, sy - 4, 6, rgb565(230, 250, 255));
  }
}

void drawCompactHud() {
  canvas.fillRoundRect(3, 3, 122, 15, 4, rgb565(8, 12, 20));
  drawBar(7, 6, 28, 3, hero.hp  hero.maxHp, rgb565(230, 94, 84), rgb565(52, 18, 22));
  drawBar(7, 10, 28, 3, hero.mp  hero.maxMp, rgb565(62, 192, 255), rgb565(18, 30, 54));
  drawBar(7, 14, 28, 2, static_castfloat(hero.xp)  static_castfloat(hero.xpNeed), rgb565(196, 154, 255), rgb565(34, 22, 52));

  canvas.setTextColor(rgb565(236, 238, 244), rgb565(8, 12, 20));
  canvas.setCursor(40, 6);
  canvas.printf(L%d %s, currentLevel, biomeName(currentBiome));
  canvas.setCursor(40, 14);
  canvas.printf(GS%d G%d, hero.gearScore, hero.gold);

  canvas.fillRoundRect(3, 109, 122, 16, 4, rgb565(8, 12, 20));
  canvas.setTextColor(rgb565(255, 224, 174), rgb565(8, 12, 20));
  canvas.setCursor(6, 113);
  canvas.print(brainLine);
  canvas.setTextColor(rgb565(164, 236, 220), rgb565(8, 12, 20));
  canvas.setCursor(6, 121);
  canvas.print((toastTimer  0.0f && toastLine[0] != '0')  toastLine  brainToken);

  canvas.setTextColor(hardMuted  rgb565(255, 154, 150)  rgb565(144, 255, 170), rgb565(8, 12, 20));
  canvas.setCursor(92, 113);
  canvas.printf(S%03d, hardMuted  0  volumeLevels[volumeIndex]);
  canvas.setCursor(92, 121);
  canvas.printf(B%03d, brightnessLevels[brightnessIndex]);
}

void drawMiniInventory() {
  for (int i = 0; i  INVENTORY_SIZE; ++i) {
    const int x = 84 + (i % 3)  13;
    const int y = 22 + (i  3)  13;
    canvas.drawRect(x, y, 11, 11, rgb565(44, 60, 84));
    canvas.fillRect(x + 1, y + 1, 9, 9, rgb565(12, 18, 28));
    if (inventory[i].active) {
      canvas.fillRect(x + 2, y + 2, 7, 7, rarityColor(inventory[i].rarity));
    }
  }
}

void drawWorld() {
  uint16_t sky = rgb565(8, 14, 22);
  switch (currentBiome) {
    case BIOME_GREEN sky = rgb565(10, 18, 20); break;
    case BIOME_MARSH sky = rgb565(8, 16, 22); break;
    case BIOME_EMBER sky = rgb565(18, 10, 10); break;
    case BIOME_RUIN  sky = rgb565(14, 10, 22); break;
  }
  canvas.fillSprite(sky);
  drawTerrain();

  drawCampSprite(toScreenX(CAMP_X), toScreenY(CAMP_Y));

  for (int i = 0; i  MAX_NODES; ++i) {
    if (!nodes[i].used  !nodes[i].active) continue;
    const int sx = toScreenX(nodes[i].x);
    const int sy = toScreenY(nodes[i].y);
    if (sx  -12  sx  SCREEN_W + 12  sy  -12  sy  SCREEN_H + 12) continue;
    drawNodeSprite(sx, sy, nodes[i]);
  }

  for (int i = 0; i  MAX_MOBS; ++i) {
    if (!mobs[i].used  !mobs[i].alive) continue;
    const int sx = toScreenX(mobs[i].x);
    const int sy = toScreenY(mobs[i].y);
    if (sx  -14  sx  SCREEN_W + 14  sy  -14  sy  SCREEN_H + 14) continue;
    drawMobSprite(sx, sy, mobs[i]);
  }

  drawHeroSprite(toScreenX(hero.x), toScreenY(hero.y));
  drawMiniInventory();
  drawCompactHud();

  if (levelTransitionPending) {
    canvas.fillRoundRect(20, 48, 88, 20, 4, rgb565(18, 12, 28));
    canvas.drawRoundRect(20, 48, 88, 20, 4, rgb565(156, 112, 220));
    canvas.setTextColor(rgb565(240, 222, 255), rgb565(18, 12, 28));
    canvas.setCursor(28, 56);
    canvas.printf(WARP TO L%d, currentLevel + 1);
  }
}

void renderFrame() {
  updateCamera();
  drawWorld();
  canvas.pushSprite(0, 0);
}

void updateTouchUi() {
  if (!M5.Touch.isEnabled()) {
    return;
  }

  auto touch = M5.Touch.getDetail();
  if (touch.wasClicked()) {
    stepBrightnessAndSoundDown();
  }

  if (touch.wasHold()) {
    toggleFullSound();
  }
}

void setupSensorSalt() {
#if HAS_BPS_SENSOR
  Wire.begin(2, 1);
  qmpReady = qmp.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 2, 1, 400000U);
  if (!qmpReady) {
    qmpReady = qmp.begin(&Wire, 0x56, 2, 1, 400000U);
  }
#endif
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(0);
  M5.Touch.setHoldThresh(3000);
  M5.Speaker.setVolume(volumeLevels[volumeIndex]);
  applyAudioVideoLevels();
  setupSensorSalt();

  canvas.createSprite(SCREEN_W, SCREEN_H);
  canvas.setTextWrap(false);
  canvas.setTextSize(1);

  randomSeed(static_castuint32_t(micros()) ^ sensorSeedSalt());
  initWorld();
  lastTickMs = millis();
}

void loop() {
  M5.update();
  updateTouchUi();

  const unsigned long now = millis();
  float dt = (now - lastTickMs)  0.001f;
  lastTickMs = now;
  if (dt  0.05f) dt = 0.05f;

  updateHero(dt);
  updateMobs(dt);
  updateNodes(dt);
  renderFrame();

  delay(16);
}
