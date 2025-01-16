#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

typedef unsigned char byte;
typedef unsigned short int word;
typedef unsigned int dword;
#define BYTE_MAX 255

#define SCREEN_WIDTH  80
#define SCREEN_HEIGHT 25

byte far *SCRBUF = (byte far *)0xB8000000L; // B800:0000 - the text buffer, "far" because it's in a different segment
byte heatbuf[SCREEN_WIDTH * (SCREEN_HEIGHT + 1)]; // +1 for the random row

// Tweak these to change the appearance of the fire

#define FLAME_CHARS           " .:;=+*#%@"  // Gradual progression of characters
#define FLAME_CHARS_LEN       10

#define FLAME_COOL_COLOR      0x07 // dark grey
#define FLAME_WARMER_COLOR    0x0E // yellow
#define FLAME_WARM_COLOR      0x0C // light red
#define FLAME_HOT_COLOR       0x04 // dark red
#define FLAME_COLOR_THRESHOLD (FLAME_CHARS_LEN / 4)


// Tweak these to change the behavior of the fire

#define NUM_FLAMES_MIN 2
#define NUM_FLAMES_MAX 6

#define BASE_HEAT_MIN 200
#define BASE_HEAT_MAX BYTE_MAX

#define IGNITION_SPREAD_MIN 5
#define IGNITION_SPREAD_MAX 20

#define IGNITION_HEAT_FLUCTUATION_MIN -20
#define IGNITION_HEAT_FLUCTUATION_MAX 20

#define SPREAD_HEAT_FLUCTUATION_MIN -3
#define SPREAD_HEAT_FLUCTUATION_MAX 3

#define DRAW_HEAT_BOOST 2


#define putcharxy(x, y, ch, color) \
    do { \
        if ((x) >= 0 && (x) < SCREEN_WIDTH && (y) >= 0 && (y) < SCREEN_HEIGHT) { \
            size_t offset = ((y) * SCREEN_WIDTH + (x)) << 1; \
            SCRBUF[offset] = (ch); \
            SCRBUF[offset + 1] = (color); \
        } \
    } while(0)

int rand_minmax(int min, int max) {
    return rand() % (max - min) + min;
}

// Sleep hs (1/100 seconds)
void sleep_hs(byte hs) {
    struct dostime_t time;
    int last_hs = 0, hs_passed = 0;

    _dos_gettime(&time);
    last_hs = time.hsecond;
    // t.hsecond contains the current second's hundredth of a second. It goes from 0 to 99 then starts again from 0.
    do {
        _dos_gettime(&time);
        if (time.hsecond < last_hs) {  
            hs_passed += 100 - last_hs + time.hsecond;
        } else {
            hs_passed += time.hsecond - last_hs;
        }
        last_hs = time.hsecond;
    } while (hs_passed < hs);
}


// ignite the fire by setting the heat value of the last row to random values
void ignite() {
    word i, x;
    byte num_flames;
    byte heat, ignition_spot, ignition_spread, delta;
    int flame_heat;

    memset(&heatbuf[SCREEN_HEIGHT * SCREEN_WIDTH], 0, SCREEN_WIDTH); // clear the last row
    
    // Create 2-4 random flame sources
    num_flames = rand_minmax(NUM_FLAMES_MIN, NUM_FLAMES_MAX);
    
    for (i = 0; i < num_flames; i++) {
        heat = rand_minmax(BASE_HEAT_MIN, BASE_HEAT_MAX);  // Higher base heat for taller flames
        ignition_spot = rand_minmax(2, SCREEN_WIDTH - 3);  // Leave some margin
        ignition_spread = rand_minmax(IGNITION_SPREAD_MIN, IGNITION_SPREAD_MAX);  // Smaller spread for multiple flames
        delta = heat / ignition_spread;
        
        // Create one flame source
        for (x = ignition_spot - ignition_spread/2; 
             x < ignition_spot + ignition_spread/2 && x < SCREEN_WIDTH - 1; 
             x++
        ) {
            // Add some fluctuation to the heat
            flame_heat = heat + rand_minmax(IGNITION_HEAT_FLUCTUATION_MIN, IGNITION_HEAT_FLUCTUATION_MAX);
            if (flame_heat > BYTE_MAX) flame_heat = BYTE_MAX;
            heatbuf[SCREEN_HEIGHT * SCREEN_WIDTH + x] = flame_heat;
            heat = (heat > delta) ? heat - delta : 0;
        }
    }
    
}

// spread the fire by calculating the heat value of a cell based on the arithmetic mean of the heat values of the 8 cells around it
void spread() {
    int x, y;
    byte new_heat;
    word sum = 0, weight_sum = 0;
    
    for (y = SCREEN_HEIGHT - 1; y >= 0; y--) {
        for (x = 0; x < SCREEN_WIDTH; x++) {
            sum = 0;
            weight_sum = 0;
            
            // Increased weight for bottom pixels to make flames rise higher
            // Bottom pixels
            if (x > 0) {
                sum += heatbuf[(y + 1) * SCREEN_WIDTH + x - 1] * 2;
                weight_sum += 2;
            }
            sum += heatbuf[(y + 1) * SCREEN_WIDTH + x] * 3;  // Center bottom has highest weight
            weight_sum += 3;
            if (x < SCREEN_WIDTH - 1) {
                sum += heatbuf[(y + 1) * SCREEN_WIDTH + x + 1] * 2;
                weight_sum += 2;
            }
            
            // Side pixels (reduced weight)
            if (x > 0) {
                sum += heatbuf[y * SCREEN_WIDTH + x - 1];
                weight_sum++;
            }
            if (x < SCREEN_WIDTH - 1) {
                sum += heatbuf[y * SCREEN_WIDTH + x + 1];
                weight_sum++;
            }
            
            // Top pixels (reduced weight to make flames rise)
            if (y > 0) {
                if (x > 0) {
                    sum += heatbuf[(y - 1) * SCREEN_WIDTH + x - 1];
                    weight_sum++;
                }
                sum += heatbuf[(y - 1) * SCREEN_WIDTH + x];
                weight_sum++;
                if (x < SCREEN_WIDTH - 1) {
                    sum += heatbuf[(y - 1) * SCREEN_WIDTH + x + 1];
                    weight_sum++;
                }
            }
            
            // Calculate new heat value with some randomness
            new_heat = (sum / weight_sum);
            
            // // Add some random fluctuation
            if (new_heat > 0) {
                int fluctuation = rand_minmax(SPREAD_HEAT_FLUCTUATION_MIN, SPREAD_HEAT_FLUCTUATION_MAX);
                new_heat = (new_heat + fluctuation > BYTE_MAX) 
                    ? BYTE_MAX 
                    : ((new_heat + fluctuation < 0) ? 0 : new_heat + fluctuation);
            }

            heatbuf[y * SCREEN_WIDTH + x] = new_heat;
        }
    }
}

// draw the fire on the screen
void draw() {
    int x, y;
    byte heat, color, char_idx;


    for (y = 0; y < SCREEN_HEIGHT; y++) {
        for (x = 0; x < SCREEN_WIDTH; x++) {
            heat = heatbuf[y * SCREEN_WIDTH + x] * DRAW_HEAT_BOOST;
            char_idx = heat * FLAME_CHARS_LEN / BYTE_MAX;

            // color by char_idx
            if (char_idx < FLAME_COLOR_THRESHOLD) color = FLAME_COOL_COLOR;
            else if (char_idx < FLAME_COLOR_THRESHOLD * 2) color = FLAME_WARMER_COLOR;
            else if (char_idx < FLAME_COLOR_THRESHOLD * 3) color = FLAME_WARM_COLOR;
            else color = FLAME_HOT_COLOR;
            

            putcharxy(x, y, FLAME_CHARS[char_idx], color);
        }
    }
}

// clear the screen
void clear() {
    memset(&heatbuf, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
    _fmemset(SCRBUF, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 2); // _fmemset because of the far pointer
}

int main()
{
    clear();
    
    do {
        ignite();
        spread();
        draw();

        sleep_hs(10);
    } while (!kbhit());

    getch(); // clear the keyboard buffer
    printf("\nDemo coded by Ferenc Faluvegi (https://ferenc.rocks).\n");

    return 0;
}
