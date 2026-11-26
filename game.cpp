#include "raylib.h"
#include <queue>
#include <cmath>
#include <cstdlib>
#include <cstring>
using namespace std;

// ================= KONSTANTA =================
const int BARIS = 21;
const int KOLOM = 19;
const int UKURAN = 30;   // piksel per sel
const int HUD = 60;      // tinggi area skor di atas
const int LEBAR = KOLOM * UKURAN;
const int TINGGI = BARIS * UKURAN + HUD;
const int JUMLAH_HANTU = 4;

// Atur kecepatan di sini (makin kecil = makin cepat)
const float LANGKAH_PACMAN = 0.14f;
const float LANGKAH_HANTU = 0.21f;
const float LANGKAH_TAKUT = 0.30f;
const float LANGKAH_MATA = 0.07f;
const float DURASI_TAKUT = 7.0f;
const float DURASI_KEJAR = 15.0f;
const float DURASI_SEBAR = 6.0f;

const int DB[4] = {-1, 1, 0, 0};   // arah: 0 atas, 1 bawah, 2 kiri, 3 kanan
const int DK[4] = {0, 0, -1, 1};

enum StatusHantu { H_MENUNGGU, H_NORMAL, H_TAKUT, H_MATA };
enum StatusGame { G_SIAP, G_MAIN, G_MATI, G_GAMEOVER, G_MENANG };

struct Posisi {
    int b, k;
};

struct Pemain {
    Posisi pos, prev;
    int arah, arahNext;
    float timer;
};

struct Hantu {
    Posisi pos, prev, sudut;
    int arah, status, kecerdasan;
    float timer, tunggu;
    Color warna;
};

const Posisi RUMAH = {9, 9};

// ================= DATA GLOBAL =================
// # dinding, . titik, o power pellet, spasi kosong
const char* petaAsli[BARIS] = {
    "###################",
    "#........#........#",
    "#o##.###.#.###.##o#",
    "#.................#",
    "#.##.#.#####.#.##.#",
    "#....#...#...#....#",
    "####.### # ###.####",
    "####.#       #.####",
    "####.# ## ## #.####",
    "    .  #   #  .    ",
    "####.# ##### #.####",
    "####.#       #.####",
    "####.# ##### #.####",
    "#........#........#",
    "#.##.###.#.###.##.#",
    "#o.#...........#.o#",
    "##.#.#.#####.#.#.##",
    "#....#...#...#....#",
    "#.######.#.######.#",
    "#.................#",
    "###################"
};

char peta[BARIS][KOLOM];            // Array 2D: peta yang sedang dimainkan
Pemain pac;
Hantu hantu[JUMLAH_HANTU];          // Array 1D: daftar hantu

int skor = 0;
int skorTertinggi = 0;
int nyawa = 3;
int sisaTitik = 0;
float waktuTakut = 0;
float faseTimer = 0;
bool modeKejar = false;
StatusGame statusGame = G_SIAP;
float statusTimer = 0;

// ================= FUNCTION =================
bool samaPosisi(Posisi a, Posisi c) {
    return a.b == c.b && a.k == c.k;
}

// sel di sebelah 'p' menurut arah; kolom tembus kiri-kanan (lorong)
Posisi tetangga(Posisi p, int arah) {
    Posisi n = {p.b + DB[arah], p.k + DK[arah]};
    if (n.k < 0) n.k = KOLOM - 1;
    if (n.k >= KOLOM) n.k = 0;
    return n;
}

bool bolehJalan(Posisi p) {
    if (p.b < 0 || p.b >= BARIS) return false;
    return peta[p.b][p.k] != '#';
}

int arahBalik(int arah) {
    if (arah < 0) return -1;
    return arah ^ 1;   // 0<->1 dan 2<->3
}

int hitungTitik() {
    int total = 0;
    for (int i = 0; i < BARIS; i++) {
        for (int j = 0; j < KOLOM; j++) {
            if (peta[i][j] == '.' || peta[i][j] == 'o') total++;
        }
    }
    return total;
}

// BFS: kembalikan ARAH langkah pertama menuju 'tujuan' (jalur terpendek), -1 kalau sudah sampai
int arahBFS(Posisi dari, Posisi tujuan) {
    if (samaPosisi(dari, tujuan)) return -1;

    bool dikunjungi[BARIS][KOLOM] = {};
    int arahAwal[BARIS][KOLOM];
    queue<Posisi> antrian;

    dikunjungi[dari.b][dari.k] = true;
    antrian.push(dari);

    while (!antrian.empty()) {
        Posisi s = antrian.front();
        antrian.pop();
        for (int i = 0; i < 4; i++) {
            Posisi n = tetangga(s, i);
            if (!bolehJalan(n) || dikunjungi[n.b][n.k]) continue;
            dikunjungi[n.b][n.k] = true;
            arahAwal[n.b][n.k] = samaPosisi(s, dari) ? i : arahAwal[s.b][s.k];
            if (samaPosisi(n, tujuan)) return arahAwal[n.b][n.k];
            antrian.push(n);
        }
    }
    return -1;
}

// arah acak yang valid (tidak balik arah kecuali buntu)
int arahAcak(const Hantu& h) {
    int pilihan[4];
    int jumlah = 0;
    int balik = arahBalik(h.arah);
    for (int i = 0; i < 4; i++) {
        if (i == balik) continue;
        if (bolehJalan(tetangga(h.pos, i))) pilihan[jumlah++] = i;
    }
    if (jumlah == 0) {
        if (balik >= 0 && bolehJalan(tetangga(h.pos, balik))) return balik;
        return -1;
    }
    return pilihan[GetRandomValue(0, jumlah - 1)];
}

float durasiLangkah(const Hantu& h) {
    if (h.status == H_MATA) return LANGKAH_MATA;
    if (h.status == H_TAKUT) return LANGKAH_TAKUT;
    return LANGKAH_HANTU;
}

bool dinding(int b, int k) {   // untuk menggambar tepi dinding
    if (b < 0 || b >= BARIS || k < 0 || k >= KOLOM) return true;
    return peta[b][k] == '#';
}

// posisi gambar (piksel) dengan gerakan halus dari prev ke pos
Vector2 posisiGambar(Posisi prev, Posisi now, float progress) {
    if (progress > 1.0f) progress = 1.0f;
    float bb = (float)now.b;
    float kk = (float)now.k;
    if (abs(now.k - prev.k) <= 1 && abs(now.b - prev.b) <= 1) {
        bb = prev.b + (now.b - prev.b) * progress;
        kk = prev.k + (now.k - prev.k) * progress;
    }
    return Vector2{(kk + 0.5f) * UKURAN, bb * UKURAN + UKURAN * 0.5f + HUD};
}

// ================= PROCEDURE: PERSIAPAN =================
void muatPeta() {
    for (int i = 0; i < BARIS; i++) {
        int panjang = (int)strlen(petaAsli[i]);
        for (int j = 0; j < KOLOM; j++) {
            peta[i][j] = (j < panjang) ? petaAsli[i][j] : '#';
        }
    }
}

void siapkanHantu() {
    Color warna[JUMLAH_HANTU] = {
        {255, 0, 0, 255}, {255, 184, 255, 255}, {0, 255, 255, 255}, {255, 184, 82, 255}
    };
    Posisi sudut[JUMLAH_HANTU] = {{1, 17}, {1, 1}, {19, 17}, {19, 1}};
    int pintar[JUMLAH_HANTU] = {90, 75, 60, 45};   // peluang hantu mengejar dengan BFS (%)
    for (int i = 0; i < JUMLAH_HANTU; i++) {
        hantu[i].warna = warna[i];
        hantu[i].sudut = sudut[i];
        hantu[i].kecerdasan = pintar[i];
    }
}

void resetPosisi() {
    pac.pos = {15, 9};
    pac.prev = pac.pos;
    pac.arah = 2;
    pac.arahNext = -1;   // diam sampai pemain menekan tombol
    pac.timer = 0;

    Posisi awal[JUMLAH_HANTU] = {{7, 9}, {9, 8}, {9, 9}, {9, 10}};
    float tunggu[JUMLAH_HANTU] = {0.0f, 3.0f, 6.0f, 9.0f};   // hantu keluar satu per satu
    for (int i = 0; i < JUMLAH_HANTU; i++) {
        hantu[i].pos = awal[i];
        hantu[i].prev = awal[i];
        hantu[i].arah = -1;
        hantu[i].timer = 0;
        hantu[i].tunggu = tunggu[i];
        hantu[i].status = (tunggu[i] > 0) ? H_MENUNGGU : H_NORMAL;
    }
    waktuTakut = 0;
    faseTimer = 0;
    modeKejar = false;
}

void mulaiBaru() {
    muatPeta();
    peta[15][9] = ' ';   // titik di posisi awal Pacman dihapus
    sisaTitik = hitungTitik();
    skor = 0;
    nyawa = 3;
    resetPosisi();
    statusGame = G_SIAP;
    statusTimer = 2.0f;
}

// ================= PROCEDURE: LOGIKA GAME =================
void aktifkanTakut() {
    waktuTakut = DURASI_TAKUT;
    for (int i = 0; i < JUMLAH_HANTU; i++) {
        if (hantu[i].status == H_NORMAL) hantu[i].status = H_TAKUT;
    }
}

void updatePacman(float dt) {
    if (pac.arahNext < 0) return;
    pac.timer += dt;
    if (pac.timer < LANGKAH_PACMAN) return;
    pac.timer -= LANGKAH_PACMAN;

    pac.prev = pac.pos;
    Posisi coba = tetangga(pac.pos, pac.arahNext);
    if (bolehJalan(coba)) {
        pac.arah = pac.arahNext;
        pac.pos = coba;
    } else {
        coba = tetangga(pac.pos, pac.arah);
        if (bolehJalan(coba)) pac.pos = coba;
    }

    char& sel = peta[pac.pos.b][pac.pos.k];
    if (sel == '.') {
        sel = ' ';
        skor += 10;
        sisaTitik--;
    } else if (sel == 'o') {
        sel = ' ';
        skor += 50;
        sisaTitik--;
        aktifkanTakut();
    }
}

void langkahHantu(Hantu& h) {
    h.prev = h.pos;
    int a = -1;

    if (h.status == H_MATA) {
        a = arahBFS(h.pos, RUMAH);
        if (a < 0) {   // sudah sampai rumah
            h.status = H_MENUNGGU;
            h.tunggu = 2.0f;
            return;
        }
    } else if (h.status == H_TAKUT) {
        a = arahAcak(h);
    } else {
        Posisi target = modeKejar ? pac.pos : h.sudut;
        if (GetRandomValue(0, 99) < h.kecerdasan) a = arahBFS(h.pos, target);
        if (a < 0) a = arahAcak(h);
    }

    if (a >= 0) {
        h.arah = a;
        h.pos = tetangga(h.pos, a);
    }
}

void updateHantu(float dt) {
    // fase: kejar Pacman <-> sebar ke pojok masing-masing
    faseTimer += dt;
    if (modeKejar && faseTimer >= DURASI_KEJAR) {
        modeKejar = false;
        faseTimer = 0;
    } else if (!modeKejar && faseTimer >= DURASI_SEBAR) {
        modeKejar = true;
        faseTimer = 0;
    }

    // hitung mundur mode takut
    if (waktuTakut > 0) {
        waktuTakut -= dt;
        if (waktuTakut <= 0) {
            for (int i = 0; i < JUMLAH_HANTU; i++) {
                if (hantu[i].status == H_TAKUT) hantu[i].status = H_NORMAL;
            }
        }
    }

    for (int i = 0; i < JUMLAH_HANTU; i++) {
        Hantu& h = hantu[i];
        if (h.status == H_MENUNGGU) {
            h.tunggu -= dt;
            if (h.tunggu <= 0) h.status = H_NORMAL;
            continue;
        }
        h.timer += dt;
        float langkah = durasiLangkah(h);
        if (h.timer < langkah) continue;
        h.timer -= langkah;
        langkahHantu(h);
    }
}

void cekTabrakan() {
    for (int i = 0; i < JUMLAH_HANTU; i++) {
        Hantu& h = hantu[i];
        // sama petak, atau saling bertukar petak dalam satu langkah
        bool bertemu = samaPosisi(h.pos, pac.pos) ||
                       (samaPosisi(h.pos, pac.prev) && samaPosisi(h.prev, pac.pos));
        if (!bertemu) continue;

        if (h.status == H_TAKUT) {
            h.status = H_MATA;
            skor += 200;
        } else if (h.status == H_NORMAL || h.status == H_MENUNGGU) {
            nyawa--;
            statusGame = G_MATI;
            statusTimer = 1.5f;
            return;
        }
    }
}

// ================= PROCEDURE: GAMBAR =================
void tulisTengah(const char* teks, int y, int ukuran, Color warna) {
    int lebar = MeasureText(teks, ukuran);
    DrawText(teks, (LEBAR - lebar) / 2, y, ukuran, warna);
}

void gambarPeta() {
    Color biru = {40, 60, 255, 255};
    Color isi = {6, 6, 30, 255};
    Color titik = {255, 184, 174, 255};

    for (int i = 0; i < BARIS; i++) {
        for (int j = 0; j < KOLOM; j++) {
            float x = (float)(j * UKURAN);
            float y = (float)(i * UKURAN + HUD);
            if (peta[i][j] == '#') {
                DrawRectangle((int)x, (int)y, UKURAN, UKURAN, isi);
                if (!dinding(i - 1, j)) DrawLineEx({x, y}, {x + UKURAN, y}, 3.0f, biru);
                if (!dinding(i + 1, j)) DrawLineEx({x, y + UKURAN}, {x + UKURAN, y + UKURAN}, 3.0f, biru);
                if (!dinding(i, j - 1)) DrawLineEx({x, y}, {x, y + UKURAN}, 3.0f, biru);
                if (!dinding(i, j + 1)) DrawLineEx({x + UKURAN, y}, {x + UKURAN, y + UKURAN}, 3.0f, biru);
            } else if (peta[i][j] == '.') {
                DrawCircle((int)(x + UKURAN / 2), (int)(y + UKURAN / 2), 3.0f, titik);
            } else if (peta[i][j] == 'o') {
                float r = 7.0f + 2.0f * sinf((float)GetTime() * 8.0f);
                DrawCircle((int)(x + UKURAN / 2), (int)(y + UKURAN / 2), r, titik);
            }
        }
    }
}

void gambarPacman() {
    float progress = pac.timer / LANGKAH_PACMAN;
    Vector2 p = posisiGambar(pac.prev, pac.pos, progress);
    const int rotasi[4] = {270, 90, 180, 0};
    float rot = (float)rotasi[pac.arah];
    float mulut = 8.0f + 32.0f * fabsf(sinf((float)GetTime() * 14.0f));

    if (statusGame == G_MATI) {   // animasi mati: mulut melebar sampai hilang
        float t = 1.0f - statusTimer / 1.5f;
        if (t < 0) t = 0;
        if (t > 0.98f) return;
        mulut = 10.0f + 170.0f * t;
        p = posisiGambar(pac.pos, pac.pos, 0);
    }
    DrawCircleSector(p, UKURAN * 0.45f, rot + mulut, rot + 360.0f - mulut, 32, YELLOW);
}

void gambarHantu(const Hantu& h) {
    float progress = h.timer / durasiLangkah(h);
    Vector2 p = posisiGambar(h.prev, h.pos, progress);
    float r = UKURAN * 0.42f;
    int a = (h.arah >= 0) ? h.arah : 2;

    if (h.status != H_MATA) {
        Color badan = h.warna;
        if (h.status == H_TAKUT) {
            badan = Color{33, 33, 255, 255};
            if (waktuTakut < 2.0f && ((int)(waktuTakut * 6.0f) % 2 == 0)) badan = WHITE;   // berkedip
        }
        DrawCircleV({p.x, p.y - r * 0.2f}, r, badan);
        DrawRectangle((int)(p.x - r), (int)(p.y - r * 0.2f), (int)(2 * r), (int)(r * 0.9f), badan);
        for (int i = -1; i <= 1; i++) {
            DrawCircleV({p.x + i * r * 0.67f, p.y + r * 0.7f}, r * 0.34f, badan);
        }
    }

    if (h.status == H_TAKUT) {
        DrawCircleV({p.x - r * 0.35f, p.y - r * 0.3f}, r * 0.14f, WHITE);
        DrawCircleV({p.x + r * 0.35f, p.y - r * 0.3f}, r * 0.14f, WHITE);
    } else {
        for (int s = -1; s <= 1; s += 2) {
            Vector2 mata = {p.x + s * r * 0.4f, p.y - r * 0.3f};
            DrawCircleV(mata, r * 0.3f, WHITE);
            DrawCircleV({mata.x + DK[a] * r * 0.12f, mata.y + DB[a] * r * 0.12f}, r * 0.15f, Color{33, 33, 255, 255});
        }
    }
}

void gambarHUD() {
    DrawText(TextFormat("SKOR  %06d", skor), 15, 8, 22, WHITE);
    DrawText(TextFormat("TERTINGGI  %06d", skorTertinggi), 15, 34, 18, GRAY);
    for (int i = 0; i < nyawa; i++) {
        DrawCircleSector({(float)(LEBAR - 30 - i * 32), 30.0f}, 11.0f, 30.0f, 330.0f, 24, YELLOW);
    }
    if (pac.arahNext < 0 && statusGame != G_GAMEOVER && statusGame != G_MENANG) {
        tulisTengah("W A S D / PANAH", 20, 16, LIGHTGRAY);
    }
}

// ================= MAIN =================
int main() {
    InitWindow(LEBAR, TINGGI, "PACMAN - Proyek Algoritma");
    SetTargetFPS(60);
    siapkanHantu();
    mulaiBaru();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        // ----- input -----
        if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)) pac.arahNext = 0;
        if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)) pac.arahNext = 1;
        if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) pac.arahNext = 2;
        if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) pac.arahNext = 3;

        // ----- update -----
        if (statusGame == G_SIAP) {
            statusTimer -= dt;
            if (statusTimer <= 0) statusGame = G_MAIN;
        } else if (statusGame == G_MAIN) {
            updatePacman(dt);
            cekTabrakan();
            if (statusGame == G_MAIN && pac.arahNext >= 0) {
                updateHantu(dt);
                cekTabrakan();
            }
            if (statusGame == G_MAIN && sisaTitik == 0) {
                statusGame = G_MENANG;
                if (skor > skorTertinggi) skorTertinggi = skor;
            }
        } else if (statusGame == G_MATI) {
            statusTimer -= dt;
            if (statusTimer <= 0) {
                if (nyawa <= 0) {
                    statusGame = G_GAMEOVER;
                    if (skor > skorTertinggi) skorTertinggi = skor;
                } else {
                    resetPosisi();
                    statusGame = G_SIAP;
                    statusTimer = 1.5f;
                }
            }
        } else {   // game over atau menang
            if (IsKeyPressed(KEY_ENTER)) mulaiBaru();
        }

        // ----- gambar -----
        BeginDrawing();
        ClearBackground(BLACK);
        gambarHUD();
        gambarPeta();
        if (statusGame != G_MATI) {
            for (int i = 0; i < JUMLAH_HANTU; i++) gambarHantu(hantu[i]);
        }
        gambarPacman();

        if (statusGame == G_SIAP) {
            tulisTengah("SIAP!", HUD + 11 * UKURAN + 3, 22, YELLOW);
        }
        if (statusGame == G_GAMEOVER || statusGame == G_MENANG) {
            int y = HUD + (BARIS * UKURAN) / 2 - 80;
            DrawRectangle(0, HUD, LEBAR, BARIS * UKURAN, Color{0, 0, 0, 180});
            if (statusGame == G_GAMEOVER) tulisTengah("GAME OVER", y, 48, RED);
            else tulisTengah("MENANG!", y, 48, GREEN);
            tulisTengah(TextFormat("Skor: %d", skor), y + 70, 26, WHITE);
            tulisTengah("Tekan ENTER untuk main lagi", y + 110, 20, LIGHTGRAY);
        }
        EndDrawing();
    }

    CloseWindow();
    return 0;
}