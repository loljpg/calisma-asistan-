#pragma once
// DAXEN VANE simge ailesi: teknik çizim karakterinde, tek çizgi kalınlığında, geometrik.
// Her simge 48×48'lik bir ızgarada komut listesidir; istenen boyuta ölçeklenip kenarı yumuşatılarak
// çizilir. Bitmap yok, kütüphane yok: hepsi birkaç yüz bayt.
//
// Komutlar (koordinatlar 0-48):
//   L x1 y1 x2 y2        çizgi
//   P n x1 y1 ... xn yn  açık çoklu çizgi
//   Q n x1 y1 ...        kapalı çokgen (çizgi)
//   R x y w h            dikdörtgen (çizgi)
//   C cx cy r            daire (çizgi)
//   A cx cy r a0 a1      yay; açılar 5'e bölünmüş, 0 = aşağı, saat yönünde (TFT_eSPI gibi)
//   D cx cy r            dolu daire
//   F x y w h            dolu dikdörtgen
//   T x1 y1 x2 y2 x3 y3  dolu üçgen
enum { IC_END = 0, IC_L, IC_P, IC_Q, IC_R, IC_C, IC_A, IC_D, IC_F, IC_T };
#define L_ IC_L,
#define P_ IC_P,
#define Q_ IC_Q,
#define R_ IC_R,
#define C_ IC_C,
#define A_ IC_A,
#define D_ IC_D,
#define F_ IC_F,
#define T_ IC_T,
#define E_ IC_END

enum Icon : uint8_t {
  I_NONE, I_HOME, I_APPS, I_SETTINGS, I_WIFI, I_WIFI_OFF, I_BT, I_SD, I_CHIP, I_CODE, I_NOTES, I_FOLDER,
  I_FILE, I_TARGET, I_TIMER, I_HEADPHONES, I_REMOTE, I_BELL, I_CALC, I_CLOCK, I_CLOUD, I_SUN, I_PARTLY,
  I_FOG, I_RAIN, I_SNOW, I_STORM, I_MOON, I_TODO, I_TRANSFER, I_PHOTO, I_BOOK, I_PENCIL, I_CARDS, I_LIGHT,
  I_GAMEPAD, I_INFO, I_BACK, I_RIGHT, I_UP, I_DOWN, I_CLOSE, I_CHECK, I_PLUS, I_MINUS, I_TRASH, I_PLAY,
  I_PAUSE, I_NEXT, I_PREV, I_SHUFFLE, I_REPEAT, I_VOLUME, I_REFRESH, I_KEYBOARD, I_ERASER, I_UNDO,
  I_LOCK, I_SEARCH, I_PHONE, I_FLAME, I_CHART, I_NOTE, I_GLOBE, I_HOURGLASS, I_FLAG, I_MINE, I_SNAKE,
  I_TILES, I_GRID, I_ROOK, I_BLOCKS, I_EYE, I_TEXT, I_SAVE, I_COPY, I_CUT, I_PASTE, I_POWER, I_UPDATE,
  I_DISPLAY, I_LIST, I_MORE, I_STOPWATCH, I_ALARM, I_DICE, I_SPEAKER_OFF, I_PULSE,
  I_COUNT
};

static const int8_t ICD_HOME[] = {P_ 3, 6, 22, 24, 8, 42, 22, P_ 4, 11, 18, 11, 40, 37, 40, 37, 18, P_ 4, 20, 40, 20, 29, 28, 29, 28, 40, E_};
static const int8_t ICD_APPS[] = {R_ 8, 8, 13, 13, R_ 27, 8, 13, 13, R_ 8, 27, 13, 13, R_ 27, 27, 13, 13, E_};
static const int8_t ICD_SETTINGS[] = {C_ 24, 24, 11, C_ 24, 24, 4, L_ 37, 24, 42, 24, L_ 33, 33, 36, 36, L_ 24, 37, 24, 42, L_ 15, 33, 12, 36,
                                      L_ 11, 24, 6, 24, L_ 15, 15, 12, 12, L_ 24, 11, 24, 6, L_ 33, 15, 36, 12, E_};
static const int8_t ICD_WIFI[] = {A_ 24, 38, 8, 27, 45, A_ 24, 38, 16, 27, 45, A_ 24, 38, 24, 28, 44, D_ 24, 38, 3, E_};
static const int8_t ICD_WIFI_OFF[] = {A_ 24, 38, 8, 27, 45, A_ 24, 38, 16, 27, 45, A_ 24, 38, 24, 28, 44, D_ 24, 38, 3, L_ 8, 8, 40, 40, E_};
static const int8_t ICD_BT[] = {P_ 6, 13, 15, 33, 33, 24, 42, 24, 6, 33, 15, 13, 33, E_};
static const int8_t ICD_SD[] = {Q_ 5, 12, 12, 18, 6, 36, 6, 36, 42, 12, 42, L_ 20, 11, 20, 17, L_ 25, 11, 25, 17, L_ 30, 11, 30, 17, E_};
static const int8_t ICD_CHIP[] = {R_ 14, 14, 20, 20, R_ 20, 20, 8, 8, L_ 19, 7, 19, 14, L_ 24, 7, 24, 14, L_ 29, 7, 29, 14,
                                  L_ 19, 34, 19, 41, L_ 24, 34, 24, 41, L_ 29, 34, 29, 41, L_ 7, 19, 14, 19, L_ 7, 24, 14, 24,
                                  L_ 7, 29, 14, 29, L_ 34, 19, 41, 19, L_ 34, 24, 41, 24, L_ 34, 29, 41, 29, E_};
static const int8_t ICD_CODE[] = {R_ 5, 9, 38, 30, P_ 3, 12, 18, 18, 24, 12, 30, L_ 22, 30, 31, 30, E_};
static const int8_t ICD_NOTES[] = {Q_ 5, 11, 6, 29, 6, 37, 14, 37, 42, 11, 42, P_ 3, 29, 6, 29, 14, 37, 14, L_ 17, 22, 31, 22, L_ 17, 28, 31, 28, L_ 17, 34, 26, 34, E_};
static const int8_t ICD_FOLDER[] = {Q_ 6, 5, 11, 18, 11, 22, 15, 43, 15, 43, 38, 5, 38, L_ 5, 20, 43, 20, E_};
static const int8_t ICD_FILE[] = {Q_ 5, 11, 6, 29, 6, 37, 14, 37, 42, 11, 42, P_ 3, 29, 6, 29, 14, 37, 14, E_};
static const int8_t ICD_TARGET[] = {C_ 24, 24, 15, C_ 24, 24, 6, L_ 24, 3, 24, 13, L_ 24, 35, 24, 45, L_ 3, 24, 13, 24, L_ 35, 24, 45, 24, E_};
static const int8_t ICD_TIMER[] = {C_ 24, 27, 15, L_ 24, 27, 24, 18, L_ 19, 5, 29, 5, L_ 24, 5, 24, 12, L_ 35, 14, 39, 10, E_};
static const int8_t ICD_HEADPHONES[] = {A_ 24, 26, 15, 18, 54, R_ 6, 26, 8, 13, R_ 34, 26, 8, 13, E_};
static const int8_t ICD_REMOTE[] = {R_ 14, 5, 20, 38, T_ 21, 18, 21, 30, 30, 24, L_ 21, 37, 27, 37, E_};
static const int8_t ICD_BELL[] = {A_ 24, 22, 10, 18, 54, L_ 14, 22, 14, 32, L_ 34, 22, 34, 32, L_ 9, 33, 39, 33, L_ 21, 39, 27, 39, L_ 24, 8, 24, 12, E_};
static const int8_t ICD_CALC[] = {R_ 10, 5, 28, 38, R_ 15, 10, 18, 8, D_ 17, 26, 2, D_ 24, 26, 2, D_ 31, 26, 2, D_ 17, 35, 2, D_ 24, 35, 2, D_ 31, 35, 2, E_};
static const int8_t ICD_CLOCK[] = {C_ 24, 24, 17, L_ 24, 24, 24, 13, L_ 24, 24, 32, 29, E_};
static const int8_t ICD_CLOUD[] = {A_ 16, 30, 8, 0, 36, A_ 26, 24, 11, 28, 59, A_ 35, 31, 7, 46, 72, L_ 16, 38, 35, 38, E_};
static const int8_t ICD_SUN[] = {C_ 24, 24, 8, L_ 24, 5, 24, 10, L_ 24, 38, 24, 43, L_ 5, 24, 10, 24, L_ 38, 24, 43, 24,
                                 L_ 11, 11, 14, 14, L_ 34, 34, 37, 37, L_ 11, 37, 14, 34, L_ 34, 14, 37, 11, E_};
static const int8_t ICD_PARTLY[] = {A_ 18, 18, 7, 18, 56, L_ 18, 4, 18, 7, L_ 4, 18, 7, 18, L_ 8, 8, 10, 10, L_ 28, 8, 26, 10,
                                    A_ 18, 34, 7, 0, 36, A_ 27, 28, 10, 28, 59, A_ 35, 35, 6, 46, 72, L_ 18, 41, 35, 41, E_};
static const int8_t ICD_FOG[] = {L_ 8, 16, 40, 16, L_ 5, 24, 37, 24, L_ 11, 32, 43, 32, L_ 8, 40, 32, 40, E_};
static const int8_t ICD_RAIN[] = {A_ 16, 24, 8, 0, 36, A_ 26, 18, 11, 28, 59, A_ 35, 25, 7, 46, 72, L_ 16, 32, 35, 32,
                                  L_ 17, 37, 15, 43, L_ 25, 37, 23, 43, L_ 33, 37, 31, 43, E_};
static const int8_t ICD_SNOW[] = {A_ 16, 24, 8, 0, 36, A_ 26, 18, 11, 28, 59, A_ 35, 25, 7, 46, 72, L_ 16, 32, 35, 32,
                                  D_ 16, 40, 2, D_ 24, 43, 2, D_ 32, 40, 2, E_};
static const int8_t ICD_STORM[] = {A_ 16, 24, 8, 0, 36, A_ 26, 18, 11, 28, 59, A_ 35, 25, 7, 46, 72, L_ 16, 32, 35, 32,
                                   P_ 4, 27, 34, 21, 40, 27, 40, 22, 46, E_};
static const int8_t ICD_MOON[] = {A_ 24, 24, 16, 57, 105, A_ 31, 17, 12, 67, 95, E_};
static const int8_t ICD_TODO[] = {P_ 3, 7, 13, 10, 16, 16, 9, L_ 21, 13, 41, 13, P_ 3, 7, 25, 10, 28, 16, 21, L_ 21, 25, 41, 25, R_ 7, 32, 8, 8, L_ 21, 36, 41, 36, E_};
static const int8_t ICD_TRANSFER[] = {L_ 16, 8, 16, 36, P_ 3, 10, 14, 16, 8, 22, 14, L_ 32, 12, 32, 40, P_ 3, 26, 34, 32, 40, 38, 34, E_};
static const int8_t ICD_PHOTO[] = {R_ 5, 9, 38, 30, P_ 5, 9, 35, 19, 23, 27, 31, 31, 27, 39, 35, C_ 32, 18, 3, E_};
static const int8_t ICD_BOOK[] = {Q_ 4, 24, 12, 7, 8, 7, 37, 24, 41, Q_ 4, 24, 12, 41, 8, 41, 37, 24, 41, E_};
static const int8_t ICD_PENCIL[] = {Q_ 5, 9, 39, 11, 30, 31, 10, 38, 17, 18, 37, L_ 27, 14, 34, 21, L_ 11, 30, 18, 37, E_};
static const int8_t ICD_CARDS[] = {R_ 16, 13, 24, 30, P_ 3, 10, 36, 10, 7, 33, 7, E_};
static const int8_t ICD_LIGHT[] = {C_ 24, 24, 7, L_ 24, 4, 24, 11, L_ 24, 37, 24, 44, L_ 4, 24, 11, 24, L_ 37, 24, 44, 24,
                                   L_ 10, 10, 15, 15, L_ 33, 33, 38, 38, L_ 10, 38, 15, 33, L_ 33, 15, 38, 10, E_};
static const int8_t ICD_GAMEPAD[] = {R_ 5, 14, 38, 22, L_ 11, 25, 21, 25, L_ 16, 20, 16, 30, D_ 30, 22, 2, D_ 36, 28, 2, E_};
static const int8_t ICD_INFO[] = {C_ 24, 24, 17, D_ 24, 15, 2, L_ 24, 21, 24, 33, E_};
static const int8_t ICD_BACK[] = {P_ 3, 29, 11, 16, 24, 29, 37, E_};
static const int8_t ICD_RIGHT[] = {P_ 3, 19, 11, 32, 24, 19, 37, E_};
static const int8_t ICD_UP[] = {P_ 3, 11, 30, 24, 17, 37, 30, E_};
static const int8_t ICD_DOWN[] = {P_ 3, 11, 18, 24, 31, 37, 18, E_};
static const int8_t ICD_CLOSE[] = {L_ 12, 12, 36, 36, L_ 36, 12, 12, 36, E_};
static const int8_t ICD_CHECK[] = {P_ 3, 9, 25, 19, 35, 39, 13, E_};
static const int8_t ICD_PLUS[] = {L_ 24, 9, 24, 39, L_ 9, 24, 39, 24, E_};
static const int8_t ICD_MINUS[] = {L_ 9, 24, 39, 24, E_};
static const int8_t ICD_TRASH[] = {L_ 8, 13, 40, 13, P_ 4, 19, 13, 19, 7, 29, 7, 29, 13, P_ 4, 12, 13, 15, 41, 33, 41, 36, 13,
                                   L_ 21, 19, 21, 35, L_ 27, 19, 27, 35, E_};
static const int8_t ICD_PLAY[] = {T_ 15, 9, 15, 39, 39, 24, E_};
static const int8_t ICD_PAUSE[] = {F_ 13, 10, 8, 28, F_ 27, 10, 8, 28, E_};
static const int8_t ICD_NEXT[] = {T_ 9, 10, 9, 38, 31, 24, F_ 32, 10, 6, 28, E_};
static const int8_t ICD_PREV[] = {T_ 39, 10, 39, 38, 17, 24, F_ 10, 10, 6, 28, E_};
static const int8_t ICD_SHUFFLE[] = {P_ 4, 5, 15, 13, 15, 29, 33, 41, 33, P_ 3, 36, 28, 41, 33, 36, 38, P_ 4, 5, 33, 13, 33, 29, 15, 41, 15,
                                     P_ 3, 36, 10, 41, 15, 36, 20, E_};
static const int8_t ICD_REPEAT[] = {P_ 3, 9, 27, 9, 15, 37, 15, P_ 3, 32, 10, 37, 15, 32, 20, P_ 3, 39, 21, 39, 33, 11, 33, P_ 3, 16, 28, 11, 33, 16, 38, E_};
static const int8_t ICD_VOLUME[] = {Q_ 6, 6, 18, 14, 18, 24, 9, 24, 39, 14, 30, 6, 30, A_ 26, 24, 8, 45, 63, A_ 26, 24, 15, 45, 63, E_};
static const int8_t ICD_SPEAKER_OFF[] = {Q_ 6, 6, 18, 14, 18, 24, 9, 24, 39, 14, 30, 6, 30, L_ 31, 18, 41, 30, L_ 41, 18, 31, 30, E_};
static const int8_t ICD_REFRESH[] = {A_ 24, 24, 15, 8, 62, P_ 3, 31, 6, 38, 12, 31, 17, E_};
static const int8_t ICD_KEYBOARD[] = {R_ 4, 12, 40, 24, D_ 11, 19, 1, D_ 18, 19, 1, D_ 25, 19, 1, D_ 32, 19, 1, D_ 38, 19, 1, D_ 11, 25, 1, D_ 18, 25, 1,
                                      D_ 25, 25, 1, D_ 32, 25, 1, D_ 38, 25, 1, L_ 15, 31, 33, 31, E_};
static const int8_t ICD_ERASER[] = {Q_ 4, 8, 30, 24, 14, 38, 28, 22, 44, L_ 16, 22, 30, 36, L_ 22, 44, 42, 44, E_};
static const int8_t ICD_UNDO[] = {P_ 3, 16, 11, 10, 17, 16, 23, L_ 10, 17, 29, 17, A_ 29, 27, 10, 36, 72, L_ 29, 37, 16, 37, E_};
static const int8_t ICD_LOCK[] = {R_ 12, 22, 24, 19, A_ 24, 22, 8, 18, 54, L_ 24, 29, 24, 34, E_};
static const int8_t ICD_SEARCH[] = {C_ 21, 21, 12, L_ 30, 30, 41, 41, E_};
static const int8_t ICD_PHONE[] = {R_ 14, 5, 20, 38, L_ 21, 37, 27, 37, E_};
static const int8_t ICD_FLAME[] = {P_ 13, 24, 4, 29, 14, 35, 21, 37, 29, 34, 37, 27, 42, 21, 42, 14, 37, 11, 29, 13, 21, 17, 26, 20, 16, 24, 4,
                                   P_ 5, 24, 26, 28, 32, 27, 37, 21, 37, 20, 32, E_};
static const int8_t ICD_CHART[] = {L_ 6, 42, 42, 42, L_ 13, 37, 13, 28, L_ 21, 37, 21, 17, L_ 29, 37, 29, 24, L_ 37, 37, 37, 10, E_};
static const int8_t ICD_NOTE[] = {C_ 16, 34, 6, L_ 22, 34, 22, 8, L_ 22, 8, 35, 13, L_ 22, 16, 35, 21, L_ 35, 13, 35, 21, E_};
static const int8_t ICD_GLOBE[] = {C_ 24, 24, 17, L_ 7, 24, 41, 24, A_ 38, 24, 22, 8, 28, A_ 10, 24, 22, 44, 64, E_};
static const int8_t ICD_HOURGLASS[] = {L_ 12, 6, 36, 6, L_ 12, 42, 36, 42, Q_ 3, 15, 6, 33, 6, 24, 24, Q_ 3, 24, 24, 33, 42, 15, 42, E_};
static const int8_t ICD_FLAG[] = {L_ 15, 7, 15, 42, Q_ 3, 15, 8, 35, 15, 15, 22, L_ 10, 42, 22, 42, E_};
static const int8_t ICD_MINE[] = {D_ 24, 24, 8, L_ 24, 7, 24, 13, L_ 24, 35, 24, 41, L_ 7, 24, 13, 24, L_ 35, 24, 41, 24,
                                  L_ 12, 12, 16, 16, L_ 32, 32, 36, 36, L_ 12, 36, 16, 32, L_ 32, 16, 36, 12, E_};
static const int8_t ICD_SNAKE[] = {P_ 6, 7, 38, 7, 28, 22, 28, 22, 16, 37, 16, 37, 30, D_ 37, 32, 3, E_};
static const int8_t ICD_TILES[] = {R_ 7, 7, 15, 15, F_ 26, 7, 15, 15, F_ 7, 26, 15, 15, R_ 26, 26, 15, 15, E_};
static const int8_t ICD_GRID[] = {R_ 6, 6, 36, 36, L_ 18, 6, 18, 42, L_ 30, 6, 30, 42, L_ 6, 18, 42, 18, L_ 6, 30, 42, 30, E_};
static const int8_t ICD_ROOK[] = {P_ 12, 15, 17, 15, 8, 20, 8, 20, 12, 22, 12, 22, 8, 26, 8, 26, 12, 28, 12, 28, 8, 33, 8, 33, 17,
                                  L_ 15, 17, 33, 17, P_ 4, 17, 17, 16, 35, 32, 35, 31, 17, L_ 11, 41, 37, 41, L_ 13, 35, 35, 35, E_};
static const int8_t ICD_BLOCKS[] = {R_ 7, 7, 15, 15, R_ 7, 26, 15, 15, R_ 26, 26, 15, 15, F_ 30, 11, 7, 7, E_};
static const int8_t ICD_EYE[] = {A_ 24, 46, 28, 28, 44, A_ 24, 2, 28, 64, 72, A_ 24, 2, 28, 0, 8, C_ 24, 24, 5, E_};
static const int8_t ICD_TEXT[] = {L_ 11, 10, 37, 10, L_ 24, 10, 24, 40, L_ 18, 40, 30, 40, E_};
static const int8_t ICD_SAVE[] = {Q_ 5, 7, 7, 34, 7, 41, 14, 41, 41, 7, 41, R_ 15, 7, 15, 10, R_ 14, 27, 20, 14, E_};
static const int8_t ICD_COPY[] = {R_ 16, 15, 23, 27, P_ 3, 9, 34, 9, 7, 30, 7, E_};
static const int8_t ICD_CUT[] = {C_ 14, 35, 6, C_ 34, 35, 6, L_ 18, 30, 33, 7, L_ 30, 30, 15, 7, E_};
static const int8_t ICD_PASTE[] = {R_ 10, 9, 28, 33, R_ 18, 5, 12, 8, L_ 17, 24, 31, 24, L_ 17, 31, 27, 31, E_};
static const int8_t ICD_POWER[] = {A_ 24, 26, 15, 42, 72, A_ 24, 26, 15, 0, 30, L_ 24, 5, 24, 24, E_};
static const int8_t ICD_UPDATE[] = {L_ 24, 7, 24, 30, P_ 3, 15, 21, 24, 30, 33, 21, P_ 4, 7, 30, 7, 41, 41, 41, 41, 30, E_};
static const int8_t ICD_DISPLAY[] = {R_ 5, 8, 38, 26, L_ 17, 41, 31, 41, L_ 24, 34, 24, 41, E_};
static const int8_t ICD_LIST[] = {L_ 17, 13, 41, 13, L_ 17, 24, 41, 24, L_ 17, 35, 41, 35, D_ 9, 13, 2, D_ 9, 24, 2, D_ 9, 35, 2, E_};
static const int8_t ICD_MORE[] = {D_ 11, 24, 3, D_ 24, 24, 3, D_ 37, 24, 3, E_};
static const int8_t ICD_STOPWATCH[] = {C_ 24, 27, 15, L_ 24, 27, 30, 21, L_ 19, 5, 29, 5, L_ 24, 5, 24, 12, E_};
static const int8_t ICD_ALARM[] = {C_ 24, 26, 15, L_ 24, 26, 24, 17, L_ 24, 26, 30, 30, L_ 7, 12, 13, 6, L_ 41, 12, 35, 6, L_ 13, 43, 16, 39, L_ 35, 43, 32, 39, E_};
static const int8_t ICD_DICE[] = {R_ 8, 8, 32, 32, D_ 16, 16, 3, D_ 32, 32, 3, D_ 24, 24, 3, D_ 32, 16, 3, D_ 16, 32, 3, E_};

static const int8_t ICD_PULSE[] = {R_ 5, 7, 38, 34, P_ 7, 9, 26, 16, 26, 20, 15, 26, 35, 31, 21, 34, 26, 39, 26, E_};

static const int8_t* const ICON_DEFS[I_COUNT] = {
    nullptr, ICD_HOME, ICD_APPS, ICD_SETTINGS, ICD_WIFI, ICD_WIFI_OFF, ICD_BT, ICD_SD, ICD_CHIP, ICD_CODE, ICD_NOTES, ICD_FOLDER,
    ICD_FILE, ICD_TARGET, ICD_TIMER, ICD_HEADPHONES, ICD_REMOTE, ICD_BELL, ICD_CALC, ICD_CLOCK, ICD_CLOUD, ICD_SUN, ICD_PARTLY,
    ICD_FOG, ICD_RAIN, ICD_SNOW, ICD_STORM, ICD_MOON, ICD_TODO, ICD_TRANSFER, ICD_PHOTO, ICD_BOOK, ICD_PENCIL, ICD_CARDS, ICD_LIGHT,
    ICD_GAMEPAD, ICD_INFO, ICD_BACK, ICD_RIGHT, ICD_UP, ICD_DOWN, ICD_CLOSE, ICD_CHECK, ICD_PLUS, ICD_MINUS, ICD_TRASH, ICD_PLAY,
    ICD_PAUSE, ICD_NEXT, ICD_PREV, ICD_SHUFFLE, ICD_REPEAT, ICD_VOLUME, ICD_REFRESH, ICD_KEYBOARD, ICD_ERASER, ICD_UNDO,
    ICD_LOCK, ICD_SEARCH, ICD_PHONE, ICD_FLAME, ICD_CHART, ICD_NOTE, ICD_GLOBE, ICD_HOURGLASS, ICD_FLAG, ICD_MINE, ICD_SNAKE,
    ICD_TILES, ICD_GRID, ICD_ROOK, ICD_BLOCKS, ICD_EYE, ICD_TEXT, ICD_SAVE, ICD_COPY, ICD_CUT, ICD_PASTE, ICD_POWER, ICD_UPDATE,
    ICD_DISPLAY, ICD_LIST, ICD_MORE, ICD_STOPWATCH, ICD_ALARM, ICD_DICE, ICD_SPEAKER_OFF, ICD_PULSE,
};

#undef L_
#undef P_
#undef Q_
#undef R_
#undef C_
#undef A_
#undef D_
#undef F_
#undef T_
#undef E_

// (x, y): sol üst köşe; size: piksel (16, 20, 24, 28 ...). bg: altındaki renk (kenar yumuşatma için).
static void drawIcon(uint8_t id, int x, int y, int size, uint16_t col, uint16_t bg) {
  if (id == I_NONE || id >= I_COUNT) return;
  const int8_t* p = ICON_DEFS[id];
  const float s = size / 48.f;
  const float sw = std::max(1.4f, size / 13.f);  // çizgi kalınlığı: 24 px'te ~1,8 px
  auto X = [&](int v) { return x + v * s; };
  auto Y = [&](int v) { return y + v * s; };
  auto line = [&](int a, int b, int c, int d) { tft.drawWideLine(X(a), Y(b), X(c), Y(d), sw, col, bg); };
  auto arc = [&](int cx, int cy, int r, int a0, int a1) {
    int ro = (int)lroundf(r * s + sw / 2), ri = std::max(0, (int)lroundf(r * s - sw / 2));
    if (ro <= ri) ro = ri + 1;
    int px = (int)lroundf(X(cx)), py = (int)lroundf(Y(cy));
    if (a1 > 360) {
      tft.drawSmoothArc(px, py, ro, ri, a0, 360, col, bg, false);
      tft.drawSmoothArc(px, py, ro, ri, 0, a1 - 360, col, bg, false);
    } else {
      tft.drawSmoothArc(px, py, ro, ri, a0, a1, col, bg, a1 - a0 < 360);
    }
  };
  for (;;) {
    switch (*p++) {
      case IC_L: line(p[0], p[1], p[2], p[3]); p += 4; break;
      case IC_P:
      case IC_Q: {
        bool closed = p[-1] == IC_Q;
        int n = *p++;
        for (int i = 0; i + 1 < n; i++) line(p[i * 2], p[i * 2 + 1], p[i * 2 + 2], p[i * 2 + 3]);
        if (closed) line(p[n * 2 - 2], p[n * 2 - 1], p[0], p[1]);
        p += n * 2;
        break;
      }
      case IC_R:
        line(p[0], p[1], p[0] + p[2], p[1]);
        line(p[0] + p[2], p[1], p[0] + p[2], p[1] + p[3]);
        line(p[0] + p[2], p[1] + p[3], p[0], p[1] + p[3]);
        line(p[0], p[1] + p[3], p[0], p[1]);
        p += 4;
        break;
      case IC_C: arc(p[0], p[1], p[2], 0, 360); p += 3; break;
      case IC_A: arc(p[0], p[1], p[2], p[3] * 5, p[4] * 5); p += 5; break;
      case IC_D: tft.fillSmoothCircle((int)lroundf(X(p[0])), (int)lroundf(Y(p[1])), std::max(1, (int)lroundf(p[2] * s * 1.2f)), col, bg); p += 3; break;
      case IC_F: tft.fillRect((int)lroundf(X(p[0])), (int)lroundf(Y(p[1])), (int)lroundf(p[2] * s), (int)lroundf(p[3] * s), col); p += 4; break;
      case IC_T:
        tft.fillTriangle((int)lroundf(X(p[0])), (int)lroundf(Y(p[1])), (int)lroundf(X(p[2])), (int)lroundf(Y(p[3])),
                         (int)lroundf(X(p[4])), (int)lroundf(Y(p[5])), col);
        p += 6;
        break;
      default: return;
    }
  }
}

// Hava durumu kodu (WMO) → simge
static uint8_t weatherIconId(int code, bool day = true) {
  if (code == 0) return day ? I_SUN : I_MOON;
  if (code <= 2) return day ? I_PARTLY : I_MOON;
  if (code == 3) return I_CLOUD;
  if (code == 45 || code == 48) return I_FOG;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return I_SNOW;
  if (code >= 95) return I_STORM;
  if (code >= 51) return I_RAIN;
  return I_CLOUD;
}

static String weatherText(int code) {
  if (code == 0) return "Açık";
  if (code <= 2) return "Parçalı bulutlu";
  if (code == 3) return "Kapalı";
  if (code == 45 || code == 48) return "Sisli";
  if (code >= 51 && code <= 57) return "Çisenti";
  if (code >= 61 && code <= 67) return "Yağmurlu";
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Karlı";
  if (code >= 80 && code <= 82) return "Sağanak";
  if (code >= 95) return "Gök gürültülü";
  return "Bulutlu";
}

// Wi-Fi sinyal çubukları (4 kademe), 16×12 alana
static void drawSignal(int x, int y, int rssi, uint16_t on, uint16_t off) {
  int lv = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi > -90 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 3;
    tft.fillRect(x + i * 4, y + 12 - h, 3, h, i < lv ? on : off);
  }
}
