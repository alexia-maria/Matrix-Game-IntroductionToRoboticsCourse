/*
	TANKS - By alexia-maria
	Tanks is a simplistic clone of the game "Battle city" from 1985.
	In this game, you play as a tank, trying to kill as many enemies as you can before you die.
	Score is how many waves of enemies you can survive before dying.

	Known bugs:
		* Game can sometimes run out of memory and crash. Unable to recreate it after lowering enemy counts.
		* No collision between player and enemies or between enemies themselves. I was unable to find a "fun" way to do collision.
		  Just stopping the player would make it so you'd get stuck way too much, so much so that you'd start feeling like the game cheats a lot.
*/

#include <Wire.h> 
#include <MD_MAX72xx.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// MD_MAX72xx DEFINITIONS
#define HARDWARE_TYPE MD_MAX72XX::DR1CR0RR1_HW
#define MAX_DEVICES 4

#define CLK_PIN 13
#define DATA_PIN 11
#define CS_PIN 8

LiquidCrystal_I2C lcd(0x27, 16, 2);
MD_MAX72XX mx(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

/*			DRAWING FUNCTIONS			*/
// MD_MAX72XX uses a strict MAX_DEVICES x 1 system.  Because our matrices
// are arranged in a 2x2 format, we have to do some pre-processing
// to our coordinates before sending them to MD_MAX72XX.
void MD_DRAW(int8_t x, int8_t y) {
	// Drop pixel if outside of render area.
	if(x < 0 || x > 15 || y < 0 || y > 15) return;

	if (y > 7) {
		y -= 8;
		x += 16;
	}
	
	mx.setPoint(y, x, true);
}

// Packing sprites that are this small(less than 8 width or height) into bytes is useless
// since we'd spend more time decoding sprite data rather than drawing it.
// We just store these sprites in code instead.
void MD_DRAW_TANK(int8_t x, int8_t y, int8_t direction) {
	switch(direction) {
		default:
		case 0: {
			MD_DRAW(x - 1, y + 1);
			MD_DRAW(x + 1, y + 1);
			MD_DRAW(x + 0, y - 0);
			MD_DRAW(x + 1, y - 0);
			MD_DRAW(x - 1, y - 0);
			MD_DRAW(x + 0, y - 1);
			break;
		}

		case 1: {
			MD_DRAW(x + 1, y - 0);
			MD_DRAW(x + 0, y - 0);
			MD_DRAW(x + 0, y - 1);
			MD_DRAW(x + 0, y + 1);
			MD_DRAW(x - 1, y - 1);
			MD_DRAW(x - 1, y + 1);
			break;
		}

		case 2: {
			MD_DRAW(x - 1, y - 1);
			MD_DRAW(x + 1, y - 1);
			MD_DRAW(x + 0, y + 0);
			MD_DRAW(x + 1, y + 0);
			MD_DRAW(x - 1, y + 0);
			MD_DRAW(x + 0, y + 1);
			break;
		}

		case 3: {
			MD_DRAW(x - 1, y - 0);
			MD_DRAW(x - 0, y - 0);
			MD_DRAW(x - 0, y - 1);
			MD_DRAW(x - 0, y + 1);
			MD_DRAW(x + 1, y - 1);
			MD_DRAW(x + 1, y + 1);
			break;
		}
	}
}
/*			DRAWING FUNCTIONS			*/



/*			CONSTANTS					*/
#define MAX_ENEMIES 4
#define MAX_BULLETS 8
/*			CONSTANTS       			*/



/*			INPUT DEFINITIONS			*/
#define CONTROLLER_X A1
#define CONTROLLER_Y A0
#define BUZZER_PIN 6
#define FIRE_BUTTON 4
#define BOOST_BUTTON 3
/*			INPUT DEFINITIONS			*/

class vec2 {
public:
	int8_t x = 0, y = 0;

	vec2(int8_t _x, int8_t _y) {
		x = _x;
		y = _y;
	}

	bool IsInArea(vec2& mins, vec2& maxs) {
		if(mins.x <= this->x && this->x <= maxs.x &&
			mins.y <= this->y && this->y <= maxs.y) {
			return true;
		}
		return false;
	}
};

#define DIR_N 0
#define DIR_E 1
#define DIR_S 2
#define DIR_W 3

class Player;
class Enemy;
class Bullet;

/*			GAME MANAGER				*/
Player* player;
Enemy* enemy_list[MAX_ENEMIES] = {};
Bullet* bullet_list[MAX_BULLETS] = {};

// Game logic and drawing is on a strict 20 ticks per second timer.
unsigned long gm_last_tick_timer = 0;

// Many objects might not want to perform an action every tick. This way,
// they can time themselves based on the tick count.
unsigned short gm_tick_count = 0;

// Enemies per wave and speed of enemies derives from wave number.
// (Both are exponential functions).
unsigned short 	gm_current_wave = 0;

// Exponential function based on wave number.
unsigned short	gm_enemies_left_to_spawn = 0;

// Timer so that we don't instantly spawn more enemies as soon as 1 wave is zover.
unsigned short 	gm_last_tick_killed_enemy = 0;

// Display manager ONLY FOR IN-GAME(HIGHSCORE SUBMISSION INCLUDED)! 
// The main menu and all of it's sub menus are handled by the LCD manager.
bool 			gm_should_update_display = true;

// Brightness of matrix displays.
int8_t			gm_matrix_brightness = 15;

#define GS_IN_MAIN_MENU 	0 // Main menu, hand over input to LCD Menu Man.
#define GS_IN_GAME 			1 // In game logic.
#define GS_IN_HIGHSCORE 	2 // High score submission.
int8_t 			gm_game_state = GS_IN_MAIN_MENU;

void GM_Update();
void GM_Shoot(vec2 pos, int8_t direction);
void GM_Reset();
void GM_Display();
void GM_AdvanceNextWave();
/*			GAME MANAGER				*/



/*			SOUND MANAGER				*/
#define SM_SOUND_HIGH 73
#define SM_SOUND_LOW 370

bool sm_sound_enabled = true;

void SM_Play_Sound(unsigned short duration, unsigned short sm_tone) {
	if(sm_sound_enabled) {
		tone(BUZZER_PIN, sm_tone, duration);
	}
}
/*			SOUND MANAGER				*/



/*			HIGH SCORE MANAGER			*/
char 	hs_name[4] = "AAA"; // 3 letters, like in old arcade games.
unsigned long hs_last_input_timer = 0;

#define HS_STATUS_ANIM_BEGIN 0
#define HS_STATUS_FIRST_LETTER 1
#define HS_STATUS_SECOND_LETTER 2
#define HS_STATUS_THIRD_LETTER 3
#define HS_STATUS_ANIM_END 4

int8_t 	hs_status = 0;
bool hs_should_update_display = false;
void HS_Update();
bool HS_Input(char* letter_ptr);
void HS_Display();

struct hs_submission {
	char name[3] = {'A', 'A', 'A'};
	uint8_t wave = 0;

	void Load(int8_t idx) {
		for(int8_t i = 0; i < 3; i++) {
			name[i] = EEPROM.read((idx * 4) + i);
		}

		wave = EEPROM.read(idx * 4 + 3);
	}

	void Save(int8_t idx) {
		for(int8_t i = 0; i < 3; i++) {
			EEPROM.update((idx * 4) + i, name[i]);
		}

		EEPROM.update((idx * 4 + 3), wave);
	}
};

hs_submission temp_submission;
hs_submission highscore_1;
hs_submission highscore_2;
hs_submission highscore_3;
/*			HIGH SCORE MANAGER			*/



/* 			LCD MENU MANAGER 			*/
// Custom LCD characters
#define LCD_HEART_LEFT_FILLED 0
byte lcd_heart_left_filled[] 	= { 0x00, 0x06, 0x0F, 0x0F, 0x0F, 0x07, 0x01, 0x00 };
#define LCD_HEART_RIGHT_FILLED 1
byte lcd_heart_right_filled[] 	= { 0x00, 0x0C, 0x1E, 0x1E, 0x1E, 0x1C, 0x10, 0x00 };

#define LCD_HEART_LEFT_OUTLINE 2
byte lcd_heart_left_outline[] 	= { 0x00, 0x06, 0x09, 0x08, 0x08, 0x06, 0x01, 0x00 };
#define LCD_HEART_RIGHT_OUTLINE 3
byte lcd_heart_right_outline[]	= { 0x00, 0x0C, 0x12, 0x02, 0x02, 0x0C, 0x10, 0x00 };

#define LCD_ARROW_UP 4
byte lcd_arrow_down[] = { 0x00, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04, 0x00 };
#define LCD_ARROW_DOWN 5
byte lcd_arrow_up[] = { 0x00, 0x04, 0x0E, 0x1F, 0x04, 0x04, 0x04, 0x00 };

class LMMEntry;
unsigned short 	lmm_current_index = 0;

unsigned short 	lmm_top_index = 0;
unsigned short 	lmm_bottom_index = 1;

LMMEntry**		lmm_entries_array = 0;
unsigned short 	lmm_entries_array_size = 0;

int				lmm_should_update_display = 0;

unsigned long 	lmm_last_input_timer = 0;
#define LMM_INPUT_DELAY 500

typedef void(*LMMCallbackFunc)();
class LMMEntry {
public:
	char  menu_name[11] = "DEBUG";
	LMMCallbackFunc menu_execute_callback = 0;

	void Click() {
		if(!menu_execute_callback) return;
		menu_execute_callback();
	}

	LMMEntry(char* name, LMMCallbackFunc func) {
		strncpy(menu_name, name, 10);
		menu_name[10] = 0x00;

		menu_execute_callback = func;
	}
};

void LMM_Update() {
	LMM_Input();

	while(lmm_should_update_display) {
		LMM_Display();
		lmm_should_update_display--;
	}
}

void LMM_Display() {
	// Invalid lmm_entries_array.
	if(lmm_entries_array == 0 || lmm_entries_array_size == 0) return;

	// Invalid lmm_top_index or lmm_bottom_index.
	if(lmm_top_index > lmm_entries_array_size || lmm_bottom_index > lmm_entries_array_size) {
		lmm_top_index = 0;
		lmm_bottom_index = 1;
		return;
	}

	// Recalculate top and bottoms.
	if(lmm_current_index > lmm_bottom_index) {
		lmm_bottom_index = lmm_current_index;
		lmm_top_index = lmm_current_index - 1;
	}else if(lmm_current_index < lmm_top_index) {
		lmm_top_index= lmm_current_index;
		lmm_bottom_index = lmm_current_index + 1;
	}

	lcd.clear();

	lcd.setCursor(15, 0);
	if(lmm_top_index != 0)
		lcd.write(LCD_ARROW_UP);

	lcd.setCursor(15, 1);
	if(lmm_bottom_index != lmm_entries_array_size - 1)
		lcd.write(LCD_ARROW_DOWN);

	lcd.setCursor(0, 0);
	lcd.print(" ");
	lcd.print(lmm_top_index + 1);
	lcd.print(". ");
	lcd.print(lmm_entries_array[lmm_top_index]->menu_name);
	lcd.setCursor(0, 1);
	lcd.print(" ");
	lcd.print(lmm_bottom_index + 1);
	lcd.print(". ");
	lcd.print(lmm_entries_array[lmm_bottom_index]->menu_name);

	lcd.setCursor(0, lmm_current_index - lmm_top_index);
	lcd.print(">");
}

void LMM_Input() {
	int y_factor = constrain((int)(((analogRead(CONTROLLER_Y) / 1023.f) - 0.5f) * 3), -1, 1);
	
	if(millis() - lmm_last_input_timer > LMM_INPUT_DELAY) {
		if(y_factor == -1) {
			if(lmm_current_index != 0) {
				lmm_current_index--;
				lmm_should_update_display++;
				lmm_last_input_timer = millis();
				SM_Play_Sound(250, SM_SOUND_HIGH);
			} 
		}else if(y_factor == 1) {
			if(lmm_current_index != lmm_entries_array_size - 1) {
				lmm_current_index++;
				lmm_should_update_display++;
				lmm_last_input_timer = millis();
				SM_Play_Sound(250, SM_SOUND_HIGH);
			}
		}
	}

	if(digitalRead(FIRE_BUTTON) == LOW) {
		lmm_entries_array[lmm_current_index]->Click();
		SM_Play_Sound(250, SM_SOUND_LOW);
		delay(500);
		lmm_should_update_display++;
	}
}

void LMM_Change_Array(void** _lmm_entries_array, unsigned short _lmm_entries_array_size) {
	lmm_current_index = 0;
	lmm_entries_array = _lmm_entries_array;
	lmm_entries_array_size = _lmm_entries_array_size;
	lmm_should_update_display++;
}

//							//
//		Menu functions		//
//							//
/*
	ROOT (lmm_sm_main_menu)
	║
	╠═════Start game [lmm_e_start_game]
	║ 
	╠══╦══Options (lmm_sm_options)[lmm_e_goto_options]
	║  ╠══╦══Matrix (lmm_sm_matrix)[lmm_e_goto_matrix]
	║  ║  ╠═════LVL(0 - 15) [lmm_e_matrix_set_#num]
	║  ║  ║
	║  ║  ╚═════GOTO Options [lmm_e_goto_options]
	║  ║
	║  ╠═════Reset Highscores [lmm_e_reset_highscores]
	║  ║
	║  ╠══╦══Sound settings (lmm_sm_sound)[lmm_e_goto_sound]
	║  ║  ╠═════ On [lmm_e_sound_on]
	║  ║  ║
	║  ║  ╠═════ Off [lm_e_sound_off]
	║  ║  ║
	║  ║  ╚═════ GOTO options [lmm_e_goto_options]
	║  ║  
	║  ╚═════GOTO Main Menu [lmm_e_goto_main_menu]
	║  
	╚══╦══Highscores (lmm_sm_highscores)[lmm_e_goto_highscores]
	   ╠═════Highscore 1 [n/a]
	   ║
	   ╠═════Highscore 2 [n/a]
	   ║
	   ╠═════Highscore 3 [n/a]
	   ║
	   ╚═════GOTO Main Menu [lmm_e_goto_main_menu]
 */

extern LMMEntry* lmm_sm_main_menu[3];
extern LMMEntry* lmm_sm_options[4];
extern LMMEntry* lmm_sm_sound[3];
extern LMMEntry* lmm_sm_matrix[17];
extern LMMEntry* lmm_sm_highscores[4];

void lmm_func_goto_sound() {
	LMM_Change_Array(lmm_sm_sound, 3);
}
LMMEntry lmm_e_goto_sound("Sound", &lmm_func_goto_sound);

void lmm_func_sound_on() {
	sm_sound_enabled = true;
	EEPROM.update(17, sm_sound_enabled);
}
LMMEntry lmm_e_sound_on("On", &lmm_func_sound_on);

void lmm_func_sound_off() {
	sm_sound_enabled = false;
	EEPROM.update(17, sm_sound_enabled);
}
LMMEntry lmm_e_sound_off("Off", &lmm_func_sound_off);

void lmm_func_start_game() {
	GM_Reset();
	gm_game_state = GS_IN_GAME;
}
LMMEntry lmm_e_start_game("Start game", &lmm_func_start_game);

void lmm_func_goto_options() {
	LMM_Change_Array(lmm_sm_options, 4);
}
LMMEntry lmm_e_goto_options("Options", &lmm_func_goto_options);

void lmm_func_goto_matrix() {
	LMM_Change_Array(lmm_sm_matrix, 17);
}
LMMEntry lmm_e_goto_matrix("Matrix", &lmm_func_goto_matrix);

#define lmm_brightness_function_generator(level)\
	void lmm_func_matrix_set_##level() { \
		gm_matrix_brightness = level; \
		mx.control(mx.INTENSITY, gm_matrix_brightness); \
		for(int x = 0; x < 16; x++){ for(int y = 0; y < 16; y++) { MD_DRAW(x, y); } } \
		mx.update(); \
		EEPROM.update(16, gm_matrix_brightness); \
	} \
	LMMEntry lmm_e_matrix_set_##level("LVL " #level, &lmm_func_matrix_set_##level);

lmm_brightness_function_generator(0);
lmm_brightness_function_generator(1);
lmm_brightness_function_generator(2);
lmm_brightness_function_generator(3);
lmm_brightness_function_generator(4);
lmm_brightness_function_generator(5);
lmm_brightness_function_generator(6);
lmm_brightness_function_generator(7);
lmm_brightness_function_generator(8);
lmm_brightness_function_generator(9);
lmm_brightness_function_generator(10);
lmm_brightness_function_generator(11);
lmm_brightness_function_generator(12);
lmm_brightness_function_generator(13);
lmm_brightness_function_generator(14);
lmm_brightness_function_generator(15);

void lmm_func_reset_highscores() {
	Serial.println("Resetting highscores...");
	memcpy(temp_submission.name, "AAA", 3);
	temp_submission.wave = 0;

	memcpy(&highscore_1, &temp_submission, sizeof(hs_submission));
	memcpy(&highscore_2, &temp_submission, sizeof(hs_submission));
	memcpy(&highscore_3, &temp_submission, sizeof(hs_submission));

	highscore_1.Save(0);
	highscore_2.Save(1);
	highscore_3.Save(2);
}
LMMEntry lmm_e_reset_highscores("Reset HS", &lmm_func_reset_highscores);

void lmm_func_goto_main_menu() {
	LMM_Change_Array(lmm_sm_main_menu, 3);
}
LMMEntry lmm_e_goto_main_menu("Main Menu", &lmm_func_goto_main_menu);

LMMEntry lmm_e_highscore_1("AAA 0", 0);
LMMEntry lmm_e_highscore_2("AAA 0", 0);
LMMEntry lmm_e_highscore_3("AAA 0", 0);

void lmm_func_goto_highscores() {
	Serial.println("Recreating the lmm_e_highscore elements...");
	
	char temp_str[16] = {};
	char temp_name_str[4] = {};
	
	memcpy(temp_name_str, highscore_1.name, 3);
	snprintf(temp_str, 15, "%s %d", temp_name_str, highscore_1.wave);
	memset(lmm_e_highscore_1.menu_name, 0, 10);
	strncpy(lmm_e_highscore_1.menu_name, temp_str, 9);

	memcpy(temp_name_str, highscore_2.name, 3);
	snprintf(temp_str, 15, "%s %d", temp_name_str, highscore_2.wave);
	memset(lmm_e_highscore_2.menu_name, 0, 10);
	strncpy(lmm_e_highscore_2.menu_name, temp_str, 9);

	memcpy(temp_name_str, highscore_3.name, 3);
	snprintf(temp_str, 15, "%s %d", temp_name_str, highscore_3.wave);
	memset(lmm_e_highscore_3.menu_name, 0, 10);
	strncpy(lmm_e_highscore_3.menu_name, temp_str, 9);

	LMM_Change_Array(lmm_sm_highscores, 4);
}
LMMEntry lmm_e_goto_highscores("Highscores", &lmm_func_goto_highscores);

LMMEntry* lmm_sm_sound[3] = {
	&lmm_e_sound_on,
	&lmm_e_sound_off,
	&lmm_e_goto_options
};

LMMEntry* lmm_sm_main_menu[3] = {
	&lmm_e_start_game,
	&lmm_e_goto_options,
	&lmm_e_goto_highscores
};

LMMEntry* lmm_sm_options[4] = {
	&lmm_e_goto_matrix,
	&lmm_e_reset_highscores,
	&lmm_e_goto_sound,
	&lmm_e_goto_main_menu
};

LMMEntry* lmm_sm_matrix[17] = {
	&lmm_e_matrix_set_0,
	&lmm_e_matrix_set_1,
	&lmm_e_matrix_set_2,
	&lmm_e_matrix_set_3,
	&lmm_e_matrix_set_4,
	&lmm_e_matrix_set_5,
	&lmm_e_matrix_set_6,
	&lmm_e_matrix_set_7,
	&lmm_e_matrix_set_8,
	&lmm_e_matrix_set_9,
	&lmm_e_matrix_set_10,
	&lmm_e_matrix_set_11,
	&lmm_e_matrix_set_12,
	&lmm_e_matrix_set_13,
	&lmm_e_matrix_set_14,
	&lmm_e_matrix_set_15,
	&lmm_e_goto_options
};

LMMEntry* lmm_sm_highscores[4] = {
	&lmm_e_highscore_1,
	&lmm_e_highscore_2,
	&lmm_e_highscore_3,
	&lmm_e_goto_main_menu
};

void LMM_Reset() {
	lmm_current_index = 0;
	lmm_top_index = 0;
	lmm_bottom_index = 1;
	lmm_entries_array = 0;
	lmm_entries_array_size = 0;
	lmm_should_update_display = 2;
	lmm_last_input_timer = 0;

	lmm_func_goto_main_menu();
}
/* 			LCD MENU MANAGER 			*/



/*			BASE ENTITY CLASS    		*/
class BaseEntity {
public:
	int8_t direction = DIR_N; // 0 - North | 1 - East | 2 - South | 3 - West
	vec2 pos = vec2(0, 0);
	vec2 aabb_mins = vec2(0, 0);
	vec2 aabb_maxs = vec2(0, 0);
	bool is_tank = true;

	void Draw() {
		MD_DRAW_TANK(pos.x, pos.y, direction);
	}

	void Move(vec2 new_pos) {
		pos.x = constrain(new_pos.x, 1, 14);
		pos.y = constrain(new_pos.y, 1, 14);

		// Recalculate AABB mins and maxs.
		aabb_mins = vec2(pos.x - 1, pos.y - 1);
		aabb_maxs = vec2(pos.x + 1, pos.y + 1);
	}

	// Relative moving
	void RelMove(vec2 rel_pos) {
		this->Move(vec2(
			constrain(pos.x + rel_pos.x, 1, 14),
			constrain(pos.y + rel_pos.y, 1, 14)
		));
	}

	void Look(int8_t new_direction) {
		direction = new_direction;
	}

	void Forward() {
		switch(direction) {
			default:
			case DIR_N:
				this->RelMove(vec2(0, -1));
				break;

			case DIR_E:
				this->RelMove(vec2(1, 0));
				break;

			case DIR_S:
				this->RelMove(vec2(0, 1));
				break;

			case DIR_W:
				this->RelMove(vec2(-1, 0));
				break;
		}
	}

	void Shoot() {
		GM_Shoot(this->pos, this->direction);
		SM_Play_Sound(100, SM_SOUND_HIGH);
	}
};
/*			BASE ENTITY CLASS    		*/



/*			PLAYER CLASS    			*/
class Player : public BaseEntity {
public:
	unsigned short last_move_gm_tick_count = 0;
	unsigned short last_shoot_gm_tick_count = 0;

	int8_t lives_left = 6;

	void Reset() {
		this->Look(DIR_N);
		this->Move(vec2(8, 8));
		last_move_gm_tick_count = 0;
		last_shoot_gm_tick_count = 0;
		lives_left = 6;
	}

	// Player logic code.
	void CreateMove() {
		if(gm_tick_count - last_move_gm_tick_count >= 3) {
			float x_factor = ((analogRead(CONTROLLER_X) / 1023.f) - 0.5f) * -1;
			float y_factor = ((analogRead(CONTROLLER_Y) / 1023.f) - 0.5f);

			if(-0.05f < x_factor && x_factor < 0.05f)
				x_factor = 0;

			if(-0.05f < y_factor && y_factor < 0.05f)
				y_factor = 0;

			// Don't update if we didn't move.
			if(x_factor != 0 || y_factor != 0) {
				// Rotate player based on direction they're moving in
				if(abs(y_factor) > abs(x_factor)) {
					if(y_factor < 0) 
						this->Look(DIR_N);
					else if(y_factor > 0)
						this->Look(DIR_S);
				} else {
					if(x_factor < 0) 
						this->Look(DIR_W);
					else if(x_factor > 0)
						this->Look(DIR_E);
				}

				vec2 new_pos = vec2(pos.x + (int)(4 * x_factor), 
									pos.y + (int)(4 * y_factor));

				this->Move(new_pos);
				last_move_gm_tick_count = gm_tick_count;
			}
		}

		if(gm_tick_count - last_shoot_gm_tick_count >= 5) {
			if(digitalRead(FIRE_BUTTON) == LOW) {
				this->Shoot();
				last_shoot_gm_tick_count = gm_tick_count;
			}
		} 
	}
};
/*			PLAYER CLASS    			*/



/*			ENEMY CLASS     			*/
class Enemy : public BaseEntity{
public:
	unsigned short last_logic_gm_tick_count = 0;

	Enemy() {
		this->Look(random(0, 4)); // Random direction.

		// We want to spawn on the opposite side of where
		// the enemy is looking.
		switch(direction) {
			default:
			case DIR_N:
				this->Move(vec2(random(1, 15), 14));
				break;

			case DIR_E:
				this->Move(vec2(random(1, 15), 1));
				break;

			case DIR_S:
				this->Move(vec2(1, random(1, 15)));
				break;

			case DIR_W:
				this->Move(vec2(14, random(1, 15)));
				break;
		}
	}

	~Enemy() {

	}

	void RunLogic() {
		int8_t rand_delay = (int)constrain(30.f * (1.f - ((gm_current_wave * gm_current_wave) * 0.003f)), 0, 30) + random(0, 8);
		if(gm_tick_count - last_logic_gm_tick_count > rand_delay) {
			// Shoot, look, run
			int8_t choice = random(0, 3);
			switch(choice) {
				default:
				case 0:
					this->Shoot();
					break;

				case 1:
					this->Forward();
					break;

				case 2:
					this->Look(random(0, 4));
					break;
			}

			last_logic_gm_tick_count = gm_tick_count;
		}
	}
};
/*			ENEMY CLASS     			*/



/*			BULLET CLASS     			*/
class Bullet {
public:
	vec2 pos = vec2(0, 0);
	int8_t direction = DIR_N;
	int8_t speed = 15;
	unsigned short last_update_gm_tick_count = 0;

	Bullet(vec2 spawn_pos, int8_t spawn_direction, int8_t spawn_speed) {
		pos = spawn_pos;
		direction = spawn_direction;
		speed = spawn_speed;
	}

	void Draw() {
		MD_DRAW(pos.x, pos.y);
	}

	void Move(vec2 new_pos) {
		pos.x = new_pos.x;
		pos.y = new_pos.y;
	}

	// Relative moving
	void RelMove(vec2 rel_pos) {
		this->Move(vec2(
			pos.x + rel_pos.x,
			pos.y + rel_pos.y
		));
	}

	void Forward() {
		switch(direction) {
			default:
			case DIR_N:
				this->RelMove(vec2(0, -1));
				break;

			case DIR_E:
				this->RelMove(vec2(1, 0));
				break;

			case DIR_S:
				this->RelMove(vec2(0, 1));
				break;

			case DIR_W:
				this->RelMove(vec2(-1, 0));
				break;
		}
	}

	bool Update() {
		if(gm_tick_count - last_update_gm_tick_count >= speed) {
			switch(direction) {
				default:
				case DIR_N:
					this->RelMove(vec2(0, -1));
					break;

				case DIR_E:
					this->RelMove(vec2(1, 0));
					break;

				case DIR_S:
					this->RelMove(vec2(0, 1));
					break;

				case DIR_W:
					this->RelMove(vec2(-1, 0));
					break;
			}

			if(pos.IsInArea(player->aabb_mins, player->aabb_maxs)) {
				SM_Play_Sound(250, SM_SOUND_LOW);
				player->lives_left--;
				if(player->lives_left == 0) {

					for(int y = 0; y < 16; y++) {
						if(y % 2 == 0) {
							for(int x = 0; x < 16; x++) {
								MD_DRAW(x, y);
								delay(5);
							}
						} else {
							for(int x = 15; x >= 0; x--) {
								MD_DRAW(x, y);
								delay(5);
							}
						}
						mx.update();
					}
					delay(2000);

					gm_game_state = GS_IN_HIGHSCORE;
				}
				gm_should_update_display = true;
				return false;
			}

			for(int i = 0; i < MAX_ENEMIES; i++) {
				Enemy* e = enemy_list[i];
				if(!e) continue;
				if(pos.IsInArea(e->aabb_mins, e->aabb_maxs)) {
					delete e;
					enemy_list[i] = nullptr;

					SM_Play_Sound(250, SM_SOUND_LOW);

					gm_last_tick_killed_enemy = gm_tick_count;

					return false;
				}
			}

			last_update_gm_tick_count = gm_tick_count;

			if(pos.x < 0 || pos.x > 15 || pos.y < 0 || pos.y > 15) {
				return false;
			}
		}

		return true;
	}
};
/*			BULLET CLASS     			*/

void setup() {
	Serial.begin(9600);

	highscore_1.Load(0);
	highscore_2.Load(1);
	highscore_3.Load(2);

	EEPROM.get(16, gm_matrix_brightness);
	EEPROM.get(17, sm_sound_enabled);

	mx.begin();

	lcd.init();
	lcd.backlight();

	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("      TANKS     ");
	lcd.setCursor(0, 1);
	lcd.print("By alexia-maria");
	delay(4000);

	lcd.createChar(LCD_HEART_LEFT_FILLED, lcd_heart_left_filled);
	lcd.createChar(LCD_HEART_RIGHT_FILLED, lcd_heart_right_filled);
	lcd.createChar(LCD_HEART_LEFT_OUTLINE, lcd_heart_left_outline);
	lcd.createChar(LCD_HEART_RIGHT_OUTLINE, lcd_heart_right_outline);
	lcd.createChar(LCD_ARROW_UP, lcd_arrow_up);
	lcd.createChar(LCD_ARROW_DOWN, lcd_arrow_down);

	// Controller input definitions
	pinMode(CONTROLLER_X, INPUT);
	pinMode(CONTROLLER_Y, INPUT);
	pinMode(FIRE_BUTTON, INPUT_PULLUP);
	pinMode(BOOST_BUTTON, INPUT_PULLUP);
	pinMode(BUZZER_PIN, OUTPUT);

	// Random seed used for RNG
	randomSeed(analogRead(A2));

	player = new Player();

	LMM_Change_Array(lmm_sm_main_menu, 3);
}

void loop() {
	switch(gm_game_state) {
		default:
		case GS_IN_MAIN_MENU: 
			LMM_Update();
			break;

		case GS_IN_GAME:
			GM_Update();
			break;

		case GS_IN_HIGHSCORE:
			HS_Update();
			break;
	}
}

void GM_Update() {
	if(millis() - gm_last_tick_timer > (1000 / 20)) {
		// Tick stage: Wave logic //
		if(gm_tick_count - gm_last_tick_killed_enemy > 60 || gm_tick_count == 0 /* Ugly hack to auto-increase wave at start of game*/ ) {
			GM_AdvanceNextWave();
		}

		if(gm_enemies_left_to_spawn > 0) {
			for(int i = 0; i < MAX_ENEMIES; i++) {
				if(!enemy_list[i]) {
					enemy_list[i] = new Enemy();
					gm_enemies_left_to_spawn--;
					return; // FIXME: 	This makes it so it only spawns 1 enemy per tick.
				}			// 			Not a huge problem, but maybe it should be noted.
			}
		}


		// Tick stage: Logic //
		player->CreateMove();
		for(Enemy* e : enemy_list) {
			if(!e) continue;
			e->RunLogic();
		}

		for(int i = 0; i < MAX_BULLETS; i++) {
			Bullet* b = bullet_list[i];
			if(!b) continue;
			if(!b->Update()) {
				delete(b);
				bullet_list[i] = 0;
			}
		}

		// Tick stage: Drawing //
		mx.control(mx.INTENSITY, gm_matrix_brightness);
		mx.clear();
		{
			player->Draw();
			for(Enemy* e : enemy_list) { 		// 2(or 3) iterations of the enemy and bullet lists in the same tick. 
				if(!e) continue;				// Not great, but I wanted to make sure every entity 
				e->Draw();						// ran it's logic before drawing them.
			}

			for(Bullet* b : bullet_list) {
				if(!b) continue;
				b->Draw();
			}
		}
		mx.update();

		// Tick stage: End //
		GM_Display();
		gm_tick_count++;
		gm_last_tick_timer = millis();
	}
}

void GM_Shoot(vec2 pos, int8_t direction) {
	for(int i = 0; i < MAX_BULLETS; i++) {
		if(!bullet_list[i]) {
			bullet_list[i] = new Bullet(pos, direction, 3);
			bullet_list[i]->Forward();
			bullet_list[i]->Forward();
			return;
		}
	}
}

void GM_Reset() {
	player->Reset();

	// Remove all enemies
	for(int i = 0; i < MAX_ENEMIES; i++) {
		if(!enemy_list[i]) continue;

		delete enemy_list[i];
		enemy_list[i] = nullptr;
	}

	for(int i = 0; i < MAX_BULLETS; i++) {
		if(!bullet_list[i]) continue;

		delete bullet_list[i];
		bullet_list[i] = nullptr;
	}

	gm_tick_count = 0;
	gm_current_wave = 0;
	gm_enemies_left_to_spawn = 0;
}

void GM_Display() {
	if(!gm_should_update_display) return;

	lcd.clear();

	lcd.setCursor(0, 0);
	lcd.print("Wave:  ");
	lcd.print(gm_current_wave);

	lcd.setCursor(0, 1);
	lcd.print("Lives: ");
	for(int i = 0; i < player->lives_left - 1; i = i + 2) {
		lcd.write(LCD_HEART_LEFT_FILLED);
		lcd.write(LCD_HEART_RIGHT_FILLED);
	}

	if(player->lives_left % 2 != 0) {
		lcd.write(LCD_HEART_LEFT_FILLED);
		lcd.write(LCD_HEART_RIGHT_OUTLINE);
	}

	for(int i = player->lives_left + 1; i < 6; i = i + 2) {
		lcd.write(LCD_HEART_LEFT_OUTLINE);
		lcd.write(LCD_HEART_RIGHT_OUTLINE);
	}

	gm_should_update_display = false;
}

void GM_AdvanceNextWave() {
	int8_t enemy_count = 0;
	for(int i = 0; i < MAX_ENEMIES; i++) { if(enemy_list[i]) enemy_count++; }

	if(gm_enemies_left_to_spawn == 0 && enemy_count == 0) {
		gm_current_wave++;
		gm_enemies_left_to_spawn = constrain((int)(30.0 * ((gm_current_wave * gm_current_wave) * 0.003f)), 1, 128);
		Serial.print("Advanced wave from ");
		Serial.print(gm_current_wave - 1);
		Serial.print(" to ");
		Serial.print(gm_current_wave);
		Serial.print(". Enemies left: ");
		Serial.println(gm_enemies_left_to_spawn);
		
		gm_should_update_display = true;
	}
}

void HS_Update() {
	switch(hs_status) {
		case HS_STATUS_ANIM_BEGIN:
				mx.clear();
				mx.update();

				lcd.clear();

				if(gm_current_wave > highscore_1.wave) {
					lcd.setCursor(0, 0);
					lcd.print("You have beaten");
					lcd.setCursor(0, 1);
					lcd.print("Highscore #1");
					hs_status = HS_STATUS_FIRST_LETTER;
				} else if(gm_current_wave > highscore_2.wave) {
					lcd.setCursor(0, 0);
					lcd.print("You have beaten");
					lcd.setCursor(0, 1);
					lcd.print("Highscore #2");
					hs_status = HS_STATUS_FIRST_LETTER;
				} else if(gm_current_wave > highscore_3.wave) {
					lcd.setCursor(0, 0);
					lcd.print("You have beaten");
					lcd.setCursor(0, 1);
					lcd.print("Highscore #3");
					hs_status = HS_STATUS_FIRST_LETTER;
				} else {
					lcd.setCursor(0, 0);
					lcd.print("You didn't beat");
					lcd.setCursor(0, 1);
					lcd.print("any highscores.");
					hs_status = HS_STATUS_ANIM_END;
				}

				hs_should_update_display = true;
				
				delay(2000);
			break;

		case HS_STATUS_FIRST_LETTER:
			HS_Input(temp_submission.name);
			break;

		case HS_STATUS_SECOND_LETTER:
			HS_Input(temp_submission.name + 1);
			break;

		case HS_STATUS_THIRD_LETTER:
			HS_Input(temp_submission.name + 2);
			break;

		default:
		case HS_STATUS_ANIM_END:
			temp_submission.wave = gm_current_wave;
			if(gm_current_wave > highscore_1.wave) {
				Serial.println("We are better than the first highscore.");
				// We are better than the first highscore.
				// We have to move all scores down(dropping the third one, obviously)
				hs_submission t;
				memcpy(&t, &highscore_2, sizeof(hs_submission));
				memcpy(&highscore_2, &highscore_1, sizeof(hs_submission));
				memcpy(&highscore_3, &t, sizeof(hs_submission));
				memcpy(&highscore_1, &temp_submission, sizeof(hs_submission));
			}else if(gm_current_wave > highscore_2.wave) {
				Serial.println("We are better than the second highscore.");
				// We are better than the second highscore.
				// We only have to move the second one down.
				memcpy(&highscore_3, &highscore_2, sizeof(hs_submission));
				memcpy(&highscore_2, &temp_submission, sizeof(hs_submission));
			}else if(gm_current_wave > highscore_3.wave) {
				Serial.println("We are better than the third highscore.");
				// We are better than the third highscore.
				memcpy(&highscore_3, &temp_submission, sizeof(hs_submission));
			}
		
			highscore_1.Save(0);
			highscore_2.Save(1);
			highscore_3.Save(2);

			LMM_Reset();
			gm_game_state = GS_IN_MAIN_MENU;
			hs_status = HS_STATUS_ANIM_BEGIN;

			delay(2000);
			break;
	}
}

bool HS_Input(char* letter_ptr) {
	HS_Display();
	int y_factor = constrain((int)(((analogRead(CONTROLLER_Y) / 1023.f) - 0.5f) * 3), -1, 1);
	
	if(millis() - hs_last_input_timer > (LMM_INPUT_DELAY / 2)) {
		if(y_factor == -1) {
			*letter_ptr = *letter_ptr - 1;
			hs_should_update_display = true;
			hs_last_input_timer = millis();
		}else if(y_factor == 1) {
			*letter_ptr = *letter_ptr + 1;
			hs_should_update_display = true;
			hs_last_input_timer = millis();
		}
	}

	*letter_ptr -= 65;
	if(*letter_ptr < 0) {
		*letter_ptr += 26;
	}

	*letter_ptr = *letter_ptr % 26;

	*letter_ptr += 65;

	if(digitalRead(FIRE_BUTTON) == LOW) {
		lcd.setCursor(5 + (hs_status), 1);
		lcd.print("$");
		hs_status++;
		delay(500);
		hs_should_update_display = true;
	}

	return !digitalRead(FIRE_BUTTON);
}

void HS_Display() {
	if(!hs_should_update_display) return;

	lcd.clear();
	lcd.setCursor(0,0);
	lcd.print("Name: ");

	lcd.print(*(temp_submission.name));
	lcd.print(*(temp_submission.name + 1));
	lcd.print(*(temp_submission.name + 2));

	lcd.setCursor(5 + (hs_status), 1);
	lcd.print("^");

	hs_should_update_display = false;
}
