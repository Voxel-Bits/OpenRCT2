#pragma region Copyright (c) 2014-2016 OpenRCT2 Developers
/*****************************************************************************
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * OpenRCT2 is the work of many authors, a full list can be found in contributors.md
 * For more information, visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * A full copy of the GNU General Public License can be found in licence.txt
 *****************************************************************************/
#pragma endregion

#include "addresses.h"
#include "cheats.h"
#include "config.h"
#include "game.h"
#include "world/climate.h"
#include "interface/viewport.h"
#include "localisation/date.h"
#include "localisation/localisation.h"
#include "management/award.h"
#include "management/finance.h"
#include "management/marketing.h"
#include "management/research.h"
#include "management/news_item.h"
#include "network/network.h"
#include "object.h"
#include "object_list.h"
#include "openrct2.h"
#include "peep/staff.h"
#include "platform/platform.h"
#include "ride/ride.h"
#include "scenario.h"
#include "title.h"
#include "util/sawyercoding.h"
#include "util/util.h"
#include "world/map.h"
#include "world/park.h"
#include "world/scenery.h"
#include "world/sprite.h"
#include "world/water.h"

const rct_string_id ScenarioCategoryStringIds[SCENARIO_CATEGORY_COUNT] = {
	STR_BEGINNER_PARKS,
	STR_CHALLENGING_PARKS,
	STR_EXPERT_PARKS,
	STR_REAL_PARKS,
	STR_OTHER_PARKS,

	STR_DLC_PARKS,
	STR_BUILD_YOUR_OWN_PARKS,
};

static char _scenarioPath[MAX_PATH];
const char *_scenarioFileName = "";

rct_s6_info *gS6Info = (rct_s6_info*)0x0141F570;
char *gScenarioName = RCT2_ADDRESS(RCT2_ADDRESS_SCENARIO_NAME, char);
char *gScenarioDetails = RCT2_ADDRESS(RCT2_ADDRESS_SCENARIO_DETAILS, char);
char *gScenarioCompletedBy = RCT2_ADDRESS(RCT2_ADDRESS_SCENARIO_COMPLETED_BY, char);
char gScenarioSavePath[MAX_PATH];
int gFirstTimeSave = 1;
uint32 gLastAutoSaveTick = 0;

#if defined(NO_RCT2)
uint32 gScenarioTicks;
#endif
uint32 gScenarioSrand0;
uint32 gScenarioSrand1;

uint8 gScenarioObjectiveType;
uint8 gScenarioObjectiveYear;
uint16 gScenarioObjectiveNumGuests;
money32 gScenarioObjectiveCurrency;

uint16 gScenarioParkRatingWarningDays;
money32 gScenarioCompletedCompanyValue;
money32 gScenarioCompanyValueRecord;

static int scenario_create_ducks();
static void scenario_objective_check();

/**
 * Loads only the basic information from a scenario.
 *  rct2: 0x006761D6
 */
bool scenario_load_basic(const char *path, rct_s6_header *header, rct_s6_info *info)
{
	log_verbose("loading scenario details, %s", path);

	SDL_RWops* rw = SDL_RWFromFile(path, "rb");
	if (rw != NULL) {
		// Read first chunk
		sawyercoding_read_chunk(rw, (uint8*)header);
		if (header->type == S6_TYPE_SCENARIO) {
			// Read second chunk
			sawyercoding_read_chunk(rw, (uint8*)info);
			SDL_RWclose(rw);
			return true;
		} else {
			log_error("invalid scenario, %s", path);
			SDL_RWclose(rw);
			return false;
		}
	}

	log_error("unable to open scenario, %s", path);
	return false;
}

int scenario_load_and_play_from_path(const char *path)
{
	window_close_construction_windows();

	if (!scenario_load(path))
		return 0;

	reset_sprite_spatial_index();
	reset_all_sprite_quadrant_placements();

	int len = strnlen(path, MAX_PATH) + 1;
	safe_strcpy(_scenarioPath, path, len);
	if (len - 1 == MAX_PATH)
	{
		_scenarioPath[MAX_PATH - 1] = '\0';
		log_warning("truncated string %s", _scenarioPath);
	}
	_scenarioFileName = path_get_filename(_scenarioPath);

	gFirstTimeSave = 1;

	log_verbose("starting scenario, %s", path);
	scenario_begin();
	if (network_get_mode() == NETWORK_MODE_SERVER) {
		network_send_map();
	}
	if (network_get_mode() == NETWORK_MODE_CLIENT) {
		network_close();
	}

	return 1;
}

void scenario_begin()
{
	rct_window *mainWindow;

	gScreenFlags = SCREEN_FLAGS_PLAYING;
	viewport_init_all();
	game_create_windows();
	mainWindow = window_get_main();

	mainWindow->viewport_target_sprite = -1;
	mainWindow->saved_view_x = gSavedViewX;
	mainWindow->saved_view_y = gSavedViewY;

	uint8 zoomDifference = gSavedViewZoom - mainWindow->viewport->zoom;
	mainWindow->viewport->zoom = gSavedViewZoom;
	gCurrentRotation = gSavedViewRotation;
	if (zoomDifference != 0) {
		if (zoomDifference < 0) {
			zoomDifference = -zoomDifference;
			mainWindow->viewport->view_width >>= zoomDifference;
			mainWindow->viewport->view_height >>= zoomDifference;
		} else {
			mainWindow->viewport->view_width <<= zoomDifference;
			mainWindow->viewport->view_height <<= zoomDifference;
		}
	}
	mainWindow->saved_view_x -= mainWindow->viewport->view_width >> 1;
	mainWindow->saved_view_y -= mainWindow->viewport->view_height >> 1;
	window_invalidate(mainWindow);

	reset_all_sprite_quadrant_placements();
	window_new_ride_init_vars();

	// Set the scenario pseudo-random seeds
	gScenarioSrand0 ^= platform_get_ticks();
	gScenarioSrand1 ^= platform_get_ticks();

	RCT2_GLOBAL(RCT2_ADDRESS_WINDOW_UPDATE_TICKS, sint16) = 0;
	gParkFlags &= ~PARK_FLAGS_NO_MONEY;
	if (gParkFlags & PARK_FLAGS_NO_MONEY_SCENARIO)
		gParkFlags |= PARK_FLAGS_NO_MONEY;
	sub_684AC3();
	scenery_set_default_placement_configuration();
	news_item_init_queue();
	if (gScenarioObjectiveType != OBJECTIVE_NONE)
		window_park_objective_open();

	gParkRating = calculate_park_rating();
	gParkValue = calculate_park_value();
	gCompanyValue = calculate_company_value();
	RCT2_GLOBAL(0x013587D0, money32) = gInitialCash - gBankLoan;
	gCashEncrypted = ENCRYPT_MONEY(gInitialCash);

	finance_update_loan_hash();

	safe_strcpy(gScenarioDetails, gS6Info->details, 256);
	safe_strcpy(gScenarioName, gS6Info->name, 64);

	{
		utf8 normalisedName[64];
		safe_strcpy(normalisedName, gS6Info->name, sizeof(normalisedName));
		scenario_normalise_name(normalisedName);

		rct_string_id localisedStringIds[3];
		if (language_get_localised_scenario_strings(normalisedName, localisedStringIds)) {
			if (localisedStringIds[0] != STR_NONE) {
				safe_strcpy(gScenarioName, language_get_string(localisedStringIds[0]), 32);
			}
			if (localisedStringIds[1] != STR_NONE) {
				park_set_name(language_get_string(localisedStringIds[1]));
			}
			if (localisedStringIds[2] != STR_NONE) {
				safe_strcpy(gScenarioDetails, language_get_string(localisedStringIds[2]), 256);
			}
		} else {
			rct_stex_entry* stex = g_stexEntries[0];
			if ((intptr_t)stex != -1) {
				char *buffer = (char*)RCT2_ADDRESS_COMMON_STRING_FORMAT_BUFFER;

				// Set localised park name
				format_string(buffer, stex->park_name, 0);
				park_set_name(buffer);

				// Set localised scenario name
				format_string(buffer, stex->scenario_name, 0);
				safe_strcpy(gScenarioName, buffer, 64);

				// Set localised scenario details
				format_string(buffer, stex->details, 0);
				safe_strcpy(gScenarioDetails, buffer, 256);
			}
		}
	}

	// Set the last saved game path
	char parkName[128];
	format_string(parkName, gParkName, &gParkNameArgs);

	platform_get_user_directory(gScenarioSavePath, "save");
	strncat(gScenarioSavePath, parkName, sizeof(gScenarioSavePath) - strlen(gScenarioSavePath) - 1);
	strncat(gScenarioSavePath, ".sv6", sizeof(gScenarioSavePath) - strlen(gScenarioSavePath) - 1);

	strcpy(gRCT2AddressSavedGamesPath2, gRCT2AddressSavedGamesPath);
	strcpy(gRCT2AddressSavedGamesPath2 + strlen(gRCT2AddressSavedGamesPath2), gScenarioSavePath);
	strcat(gRCT2AddressSavedGamesPath2, ".SV6");

	memset((void*)0x001357848, 0, 56);
	gCurrentExpenditure = 0;
	gCurrentProfit = 0;
	gWeeklyProfitAverageDividend = 0;
	gWeeklyProfitAverageDivisor = 0;
	gScenarioCompletedCompanyValue = MONEY32_UNDEFINED;
	gTotalAdmissions = 0;
	gTotalIncomeFromAdmissions = 0;
	RCT2_GLOBAL(0x013587D8, uint16) = 63;
	finance_update_loan_hash();
	park_reset_history();
	finance_reset_history();
	award_reset();
	reset_all_ride_build_dates();
	date_reset();
	duck_remove_all();
	park_calculate_size();
	staff_reset_stats();
	RCT2_GLOBAL(RCT2_ADDRESS_LAST_ENTRANCE_STYLE, uint8) = 0;
	memset((void*)0x001358102, 0, 20);
	RCT2_GLOBAL(0x00135882E, uint16) = 0;

	// Open park with free entry when there is no money
	if (gParkFlags & PARK_FLAGS_NO_MONEY) {
		gParkFlags |= PARK_FLAGS_PARK_OPEN;
		gParkEntranceFee = 0;
	}

	gParkFlags |= PARK_FLAGS_18;

	load_palette();

	gfx_invalidate_screen();
	gScreenAge = 0;
	gGameSpeed = 1;
}

static void scenario_end()
{
	rct_window* w;
	window_close_by_class(WC_DROPDOWN);

	for (w = g_window_list; w < gWindowNextSlot; w++){
		if (!(w->flags & (WF_STICK_TO_BACK | WF_STICK_TO_FRONT)))
			window_close(w);
	}
	window_park_objective_open();
}

void scenario_set_filename(const char *value)
{
	substitute_path(_scenarioPath, gRCT2AddressScenariosPath, value);
	_scenarioFileName = path_get_filename(_scenarioPath);
}

/**
 *
 *  rct2: 0x0066A752
 */
void scenario_failure()
{
	gScenarioCompletedCompanyValue = 0x80000001;
	scenario_end();
}

/**
 *
 *  rct2: 0x0066A75E
 */
void scenario_success()
{
	const money32 companyValue = gCompanyValue;

	gScenarioCompletedCompanyValue = companyValue;
	peep_applause();

	scenario_index_entry *scenario = scenario_list_find_by_filename(_scenarioFileName);
	if (scenario != NULL) {
		// Check if record company value has been broken
		if (scenario->highscore == NULL || scenario->highscore->company_value < companyValue) {
			if (scenario->highscore == NULL) {
				scenario->highscore = scenario_highscore_insert();
			} else {
				scenario_highscore_free(scenario->highscore);
			}
			scenario->highscore->fileName = _strdup(path_get_filename(scenario->path));
			scenario->highscore->name = NULL;
			scenario->highscore->company_value = companyValue;
			scenario->highscore->timestamp = platform_get_datetime_now_utc();

			// Allow name entry
			gParkFlags |= PARK_FLAGS_SCENARIO_COMPLETE_NAME_INPUT;
			gScenarioCompanyValueRecord = companyValue;
			scenario_scores_save();
		}
	}
	scenario_end();
}

/**
 *
 *  rct2: 0x006695E8
 */
void scenario_success_submit_name(const char *name)
{
	scenario_index_entry *scenario = scenario_list_find_by_filename(_scenarioFileName);
	if (scenario != NULL) {
		money32 scenarioWinCompanyValue = gScenarioCompanyValueRecord;
		if (scenario->highscore->company_value == scenarioWinCompanyValue) {
			scenario->highscore->name = _strdup(name);
			safe_strcpy(gScenarioCompletedBy, name, 32);
			scenario_scores_save();
		}
	}

	gParkFlags &= ~PARK_FLAGS_SCENARIO_COMPLETE_NAME_INPUT;
}

/**
 * Send a warning when entrance price is too high.
 *  rct2: 0x0066A80E
 */
static void scenario_entrance_fee_too_high_check()
{
	uint16 x = 0, y = 0;
	money16 totalRideValue = gTotalRideValue;
	money16 max_fee = totalRideValue + (totalRideValue / 2);

	if ((gParkFlags & PARK_FLAGS_PARK_OPEN) && park_get_entrance_fee() > max_fee) {
		for (int i = 0; gParkEntranceX[i] != SPRITE_LOCATION_NULL; i++) {
			x = gParkEntranceX[i] + 16;
			y = gParkEntranceY[i] + 16;
		}

		uint32 packed_xy = (y << 16) | x;
		if (gConfigNotifications.park_warnings) {
			news_item_add_to_queue(NEWS_ITEM_BLANK, STR_ENTRANCE_FEE_TOO_HI, packed_xy);
		}
	}
}

void scenario_autosave_check()
{
	// Milliseconds since last save
	uint32 timeSinceSave = SDL_GetTicks() - gLastAutoSaveTick;

	bool shouldSave = false;
	switch (gConfigGeneral.autosave_frequency) {
	case AUTOSAVE_EVERY_MINUTE:
		shouldSave = timeSinceSave >= 1 * 60 * 1000;
		break;
	case AUTOSAVE_EVERY_5MINUTES:
		shouldSave = timeSinceSave >= 5 * 60 * 1000;
		break;
	case AUTOSAVE_EVERY_15MINUTES:
		shouldSave = timeSinceSave >= 15 * 60 * 1000;
		break;
	case AUTOSAVE_EVERY_30MINUTES:
		shouldSave = timeSinceSave >= 30 * 60 * 1000;
		break;
	case AUTOSAVE_EVERY_HOUR:
		shouldSave = timeSinceSave >= 60 * 60 * 1000;
		break;
	}

	if (shouldSave) {
		gLastAutoSaveTick = SDL_GetTicks();
		game_autosave();
	}
}

static void scenario_day_update()
{
	finance_update_daily_profit();
	peep_update_days_in_queue();
	switch (gScenarioObjectiveType) {
	case OBJECTIVE_10_ROLLERCOASTERS:
	case OBJECTIVE_GUESTS_AND_RATING:
	case OBJECTIVE_10_ROLLERCOASTERS_LENGTH:
	case OBJECTIVE_FINISH_5_ROLLERCOASTERS:
	case OBJECTIVE_REPLAY_LOAN_AND_PARK_VALUE:
		scenario_objective_check();
		break;
	}

	uint16 unk = (gParkFlags & PARK_FLAGS_NO_MONEY) ? 40 : 7;
	RCT2_GLOBAL(0x00135882E, uint16) = RCT2_GLOBAL(0x00135882E, uint16) > unk ? RCT2_GLOBAL(0x00135882E, uint16) - unk : 0;

	gToolbarDirtyFlags |= BTM_TB_DIRTY_FLAG_DATE;
}

static void scenario_week_update()
{
	int month = gDateMonthsElapsed & 7;

	finance_pay_wages();
	finance_pay_research();
	finance_pay_interest();
	marketing_update();
	peep_problem_warnings_update();
	ride_check_all_reachable();
	ride_update_favourited_stat();

	rct_water_type* water_type = (rct_water_type*)object_entry_groups[OBJECT_TYPE_WATER].chunks[0];

	if (month <= MONTH_APRIL && (intptr_t)water_type != -1 && water_type->var_0E & 1) {
		// 100 attempts at finding some water to create a few ducks at
		for (int i = 0; i < 100; i++) {
			if (scenario_create_ducks())
				break;
		}
	}
	park_update_histories();
	park_calculate_size();
}

static void scenario_fortnight_update()
{
	finance_pay_ride_upkeep();
}

static void scenario_month_update()
{
	finance_shift_expenditure_table();
	scenario_objective_check();
	scenario_entrance_fee_too_high_check();
	award_update_all();
}

static void scenario_update_daynight_cycle()
{
	float currentDayNightCycle = gDayNightCycle;
	gDayNightCycle = 0;

	if (gScreenFlags == SCREEN_FLAGS_PLAYING && gConfigGeneral.day_night_cycle) {
		float monthFraction = gDateMonthTicks / (float)0x10000;
		if (monthFraction < (1 / 8.0f)) {
			gDayNightCycle = 0.0f;
		} else if (monthFraction < (3 / 8.0f)) {
			gDayNightCycle = (monthFraction - (1 / 8.0f)) / (2 / 8.0f);
		} else if (monthFraction < (5 / 8.0f)) {
			gDayNightCycle = 1.0f;
		} else if (monthFraction < (7 / 8.0f)) {
			gDayNightCycle = 1.0f - ((monthFraction - (5 / 8.0f)) / (2 / 8.0f));
		} else {
			gDayNightCycle = 0.0f;
		}
	}

	// Only update palette if day / night cycle has changed
	if (gDayNightCycle != currentDayNightCycle) {
		platform_update_palette(gGamePalette, 10, 236);
	}
}

/**
 * Scenario and finance related update iteration.
 *  rct2: 0x006C44B1
 */
void scenario_update()
{
	if (!(gScreenFlags & ~SCREEN_FLAGS_PLAYING)) {
		uint32 currentMonthTick = gDateMonthTicks;
		uint32 nextMonthTick = currentMonthTick + 4;
		uint8 currentMonth = gDateMonthsElapsed & 7;
		uint8 currentDaysInMonth = (uint8)days_in_month[currentMonth];

		if ((currentDaysInMonth * nextMonthTick) >> 16 != (currentDaysInMonth * currentMonthTick) >> 16) {
			scenario_day_update();
		}
		if (nextMonthTick % 0x4000 == 0) {
			scenario_week_update();
		}
		if (nextMonthTick % 0x8000 == 0) {
			scenario_fortnight_update();
		}

		gDateMonthTicks = (uint16)nextMonthTick;
		if (nextMonthTick >= 0x10000) {
			gDateMonthsElapsed++;
			scenario_month_update();
		}
	}

	scenario_update_daynight_cycle();
}

/**
 *
 *  rct2: 0x006744A9
 */
static int scenario_create_ducks()
{
	int i, j, r, c, x, y, waterZ, centreWaterZ, x2, y2;

	r = scenario_rand();
	x = ((r >> 16) & 0xFFFF) & 0x7F;
	y = (r & 0xFFFF) & 0x7F;
	x = (x + 64) * 32;
	y = (y + 64) * 32;

	if (!map_is_location_in_park(x, y))
		return 0;

	centreWaterZ = (map_element_height(x, y) >> 16) & 0xFFFF;
	if (centreWaterZ == 0)
		return 0;

	// Check 7x7 area around centre tile
	x2 = x - (32 * 3);
	y2 = y - (32 * 3);
	c = 0;
	for (i = 0; i < 7; i++) {
		for (j = 0; j < 7; j++) {
			waterZ = (map_element_height(x2, y2) >> 16) & 0xFFFF;
			if (waterZ == centreWaterZ)
				c++;

			x2 += 32;
		}
		x2 -= 224;
		y2 += 32;
	}

	// Must be at least 25 water tiles of the same height in 7x7 area
	if (c < 25)
		return 0;

	// Set x, y to the centre of the tile
	x += 16;
	y += 16;
	c = (scenario_rand() & 3) + 2;
	for (i = 0; i < c; i++) {
		r = scenario_rand();
		x2 = (r >> 16) & 0x7F;
		y2 = (r & 0xFFFF) & 0x7F;
		create_duck(x + x2 - 64, y + y2 - 64);
	}

	return 1;
}

/**
 *
 *  rct2: 0x006E37D2
 *
 * @return eax
 */
unsigned int scenario_rand()
{
#ifdef DEBUG_DESYNC
	if (!gInUpdateCode) {
		log_warning("scenario_rand called from outside game update");
		assert(false);
	}
#endif

	uint32 originalSrand0 = gScenarioSrand0;
	gScenarioSrand0 += ror32(gScenarioSrand1 ^ 0x1234567F, 7);
	return gScenarioSrand1 = ror32(originalSrand0, 3);
}

unsigned int scenario_rand_max(unsigned int max)
{
	if (max < 2) return 0;
	if ((max & (max - 1)) == 0)
		return scenario_rand() & (max - 1);
	unsigned int rand, cap = ~((unsigned int)0) - (~((unsigned int)0) % max) - 1;
	do {
		rand = scenario_rand();
	} while (rand > cap);
	return rand % max;
}

/**
 * Prepare rides, for the finish five rollercoasters objective.
 *  rct2: 0x006788F7
 */
static void scenario_prepare_rides_for_save()
{
	int i;
	rct_ride *ride;
	map_element_iterator it;

	int isFiveCoasterObjective = gScenarioObjectiveType == OBJECTIVE_FINISH_5_ROLLERCOASTERS;

	// Set all existing track to be indestructible
	map_element_iterator_begin(&it);
	do {
		if (map_element_get_type(it.element) == MAP_ELEMENT_TYPE_TRACK) {
			if (isFiveCoasterObjective)
				it.element->flags |= 0x40;
			else
				it.element->flags &= ~0x40;
		}
	} while (map_element_iterator_next(&it));

	// Set all existing rides to have indestructible track
	FOR_ALL_RIDES(i, ride) {
		if (isFiveCoasterObjective)
			ride->lifecycle_flags |= RIDE_LIFECYCLE_INDESTRUCTIBLE_TRACK;
		else
			ride->lifecycle_flags &= ~RIDE_LIFECYCLE_INDESTRUCTIBLE_TRACK;
	}
}

/**
 *
 *  rct2: 0x006726C7
 */
int scenario_prepare_for_save()
{
	rct_s6_info *s6Info = gS6Info;
	char buffer[256];

	s6Info->entry.flags = 255;

	rct_stex_entry* stex = g_stexEntries[0];
	if ((intptr_t)stex != -1) {
		format_string(buffer, stex->scenario_name, NULL);
		safe_strcpy(s6Info->name, buffer, sizeof(s6Info->name));

		memcpy(&s6Info->entry, &object_entry_groups[OBJECT_TYPE_SCENARIO_TEXT].entries[0], sizeof(rct_object_entry));
	}

	if (s6Info->name[0] == 0)
		format_string(s6Info->name, gParkName, &gParkNameArgs);

	s6Info->objective_type = gScenarioObjectiveType;
	s6Info->objective_arg_1 = gScenarioObjectiveYear;
	s6Info->objective_arg_2 = gScenarioObjectiveCurrency;
	s6Info->objective_arg_3 = gScenarioObjectiveNumGuests;

	scenario_prepare_rides_for_save();

	if (gScenarioObjectiveType == OBJECTIVE_GUESTS_AND_RATING)
		gParkFlags |= PARK_FLAGS_PARK_OPEN;

	// Fix #2385: saved scenarios did not initialise temperatures to selected climate
	climate_reset(gClimate);

	return 1;
}

/**
 *
 *  rct2: 0x006AA244
 */
int scenario_get_num_packed_objects_to_write()
{
	int count = 0;
	for (int i = 0; i < OBJECT_ENTRY_COUNT; i++) {
		const rct_object_entry *entry = get_loaded_object_entry(i);
		void *entryData = get_loaded_object_chunk(i);
		if (entryData != (void*)0xFFFFFFFF && !(entry->flags & 0xF0)) {
			count++;
		}
	}
	return count;
}

/**
 *
 *  rct2: 0x006AA26E
 */
int scenario_write_packed_objects(SDL_RWops* rw)
{
	for (int i = 0; i < OBJECT_ENTRY_COUNT; i++) {
		const rct_object_entry *entry = get_loaded_object_entry(i);
		void *entryData = get_loaded_object_chunk(i);
		if (entryData != (void*)0xFFFFFFFF && !(entry->flags & 0xF0)) {
			if (!object_saved_packed(rw, entry)) {
				return 0;
			}
		}
	}
	return 1;
}

/**
 *
 *  rct2: 0x006AA039
 */
static int scenario_write_available_objects(FILE *file)
{
	uint8 *buffer, *dstBuffer;
	int i, encodedLength;
	sawyercoding_chunk_header chunkHeader;

	const int totalEntries = OBJECT_ENTRY_COUNT;
	const int bufferLength = totalEntries * sizeof(rct_object_entry);

	// Initialise buffers
	buffer = malloc(bufferLength);
	if (buffer == NULL) {
		log_error("out of memory");
		return 0;
	}
	dstBuffer = malloc(bufferLength + sizeof(sawyercoding_chunk_header));
	if (dstBuffer == NULL) {
		free(buffer);
		log_error("out of memory");
		return 0;
	}

	// Write entries
	rct_object_entry *dstEntry = (rct_object_entry*)buffer;
	for (i = 0; i < OBJECT_ENTRY_COUNT; i++) {
		void *entryData = get_loaded_object_chunk(i);
		if (entryData == (void*)0xFFFFFFFF) {
			memset(dstEntry, 0xFF, sizeof(rct_object_entry));
		} else {
			*dstEntry = *get_loaded_object_entry(i);
		}
		dstEntry++;
	}

	// Write chunk
	chunkHeader.encoding = CHUNK_ENCODING_ROTATE;
	chunkHeader.length = bufferLength;
	encodedLength = sawyercoding_write_chunk_buffer(dstBuffer, buffer, chunkHeader);
	fwrite(dstBuffer, encodedLength, 1, file);

	// Free buffers
	free(dstBuffer);
	free(buffer);
	return 1;
}

/**
 * Modifies the given S6 data so that ghost elements, rides with no track elements or unused banners / user strings are saved.
 */
void scenario_fix_ghosts(rct_s6_data *s6)
{
	// Remove all ghost elements
	rct_map_element *destinationElement = s6->map_elements;

	for (int y = 0; y < 256; y++) {
		for (int x = 0; x < 256; x++) {
			rct_map_element *originalElement = map_get_first_element_at(x, y);
			do {
				if (originalElement->flags & MAP_ELEMENT_FLAG_GHOST) {
					int bannerIndex = map_element_get_banner_index(originalElement);
					if (bannerIndex != -1) {
						rct_banner *banner = &s6->banners[bannerIndex];
						if (banner->type != BANNER_NULL) {
							banner->type = BANNER_NULL;
							if (is_user_string_id(banner->string_idx))
								s6->custom_strings[(banner->string_idx % MAX_USER_STRINGS) * USER_STRING_MAX_LENGTH] = 0;
						}
					}
				} else {
					*destinationElement++ = *originalElement;
				}
			} while (!map_element_is_last_for_tile(originalElement++));

			// Set last element flag in case the original last element was never added
			(destinationElement - 1)->flags |= MAP_ELEMENT_FLAG_LAST_TILE;
		}
	}
}

void scenario_remove_trackless_rides(rct_s6_data *s6)
{
	bool rideHasTrack[MAX_RIDES];
	ride_all_has_any_track_elements(rideHasTrack);
	for (int i = 0; i < MAX_RIDES; i++) {
		rct_ride *ride = &s6->rides[i];

		if (rideHasTrack[i] || ride->type == RIDE_TYPE_NULL) {
			continue;
		}

		ride->type = RIDE_TYPE_NULL;
		if (is_user_string_id(ride->name)) {
			s6->custom_strings[(ride->name % MAX_USER_STRINGS) * USER_STRING_MAX_LENGTH] = 0;
		}
	}
}

static void scenario_objective_check_guests_by()
{
	uint8 objectiveYear = gScenarioObjectiveYear;
	sint16 parkRating = gParkRating;
	sint16 guestsInPark = gNumGuestsInPark;
	sint16 objectiveGuests = gScenarioObjectiveNumGuests;
	sint16 currentMonthYear = gDateMonthsElapsed;

	if (currentMonthYear == 8 * objectiveYear){
		if (parkRating >= 600 && guestsInPark >= objectiveGuests)
			scenario_success();
		else
			scenario_failure();
	}
}

static void scenario_objective_check_park_value_by()
{
	uint8 objectiveYear = gScenarioObjectiveYear;
	sint16 currentMonthYear = gDateMonthsElapsed;
	money32 objectiveParkValue = gScenarioObjectiveCurrency;
	money32 parkValue = gParkValue;

	if (currentMonthYear == 8 * objectiveYear) {
		if (parkValue >= objectiveParkValue)
			scenario_success();
		else
			scenario_failure();
	}
}

/**
* Checks if there are 10 rollercoasters of different subtype with
* excitement >= 600 .
* rct2:
**/
static void scenario_objective_check_10_rollercoasters()
{
	int i, rcs = 0;
	uint8 type_already_counted[256];
	rct_ride* ride;

	memset(type_already_counted, 0, 256);

	FOR_ALL_RIDES(i, ride) {
		uint8 subtype_id = ride->subtype;
		rct_ride_entry *rideType = get_ride_entry(subtype_id);
		if (rideType == NULL) {
			continue;
		}

		if (rideType != NULL &&
			(rideType->category[0] == RIDE_GROUP_ROLLERCOASTER || rideType->category[1] == RIDE_GROUP_ROLLERCOASTER) &&
			ride->status == RIDE_STATUS_OPEN &&
			ride->excitement >= RIDE_RATING(6,00) && type_already_counted[subtype_id] == 0){
			type_already_counted[subtype_id]++;
			rcs++;
		}
	}

	if (rcs >= 10)
		scenario_success();
}

/**
 *
 *  rct2: 0x0066A13C
 */
static void scenario_objective_check_guests_and_rating()
{
	if (gParkRating < 700 && gDateMonthsElapsed >= 1) {
		gScenarioParkRatingWarningDays++;
		if (gScenarioParkRatingWarningDays == 1) {
			if (gConfigNotifications.park_rating_warnings) {
				news_item_add_to_queue(NEWS_ITEM_GRAPH, STR_PARK_RATING_WARNING_4_WEEKS_REMAINING, 0);
			}
		} else if (gScenarioParkRatingWarningDays == 8) {
			if (gConfigNotifications.park_rating_warnings) {
				news_item_add_to_queue(NEWS_ITEM_GRAPH, STR_PARK_RATING_WARNING_3_WEEKS_REMAINING, 0);
			}
		} else if (gScenarioParkRatingWarningDays == 15) {
			if (gConfigNotifications.park_rating_warnings) {
				news_item_add_to_queue(NEWS_ITEM_GRAPH, STR_PARK_RATING_WARNING_2_WEEKS_REMAINING, 0);
			}
		} else if (gScenarioParkRatingWarningDays == 22) {
			if (gConfigNotifications.park_rating_warnings) {
				news_item_add_to_queue(NEWS_ITEM_GRAPH, STR_PARK_RATING_WARNING_1_WEEK_REMAINING, 0);
			}
		} else if (gScenarioParkRatingWarningDays == 29) {
			news_item_add_to_queue(NEWS_ITEM_GRAPH, STR_PARK_HAS_BEEN_CLOSED_DOWN, 0);
			gParkFlags &= ~PARK_FLAGS_PARK_OPEN;
			scenario_failure();
			gGuestInitialHappiness = 50;
		}
	} else if (gScenarioCompletedCompanyValue != 0x80000001) {
		gScenarioParkRatingWarningDays = 0;
	}

	if (gParkRating >= 700)
		if (gNumGuestsInPark >= gScenarioObjectiveNumGuests)
			scenario_success();
}

static void scenario_objective_check_monthly_ride_income()
{
	money32 *expenditureLastMonth = &gExpenditureTable[1 * RCT_EXPENDITURE_TYPE_COUNT];
	money32 lastMonthRideIncome = expenditureLastMonth[RCT_EXPENDITURE_TYPE_PARK_RIDE_TICKETS];
	if (lastMonthRideIncome >= gScenarioObjectiveCurrency) {
		scenario_success();
	}
}

/**
 * Checks if there are 10 rollercoasters of different subtype with
 * excitement > 700 and a minimum length;
 *  rct2: 0x0066A6B5
 */
static void scenario_objective_check_10_rollercoasters_length()
{
	int i, rcs = 0;
	uint8 type_already_counted[256];
	sint16 objective_length = gScenarioObjectiveNumGuests;
	rct_ride* ride;

	memset(type_already_counted, 0, 256);

	FOR_ALL_RIDES(i, ride) {
		uint8 subtype_id = ride->subtype;
		rct_ride_entry *rideType = get_ride_entry(subtype_id);
		if (rideType == NULL) {
			continue;
		}
		if ((rideType->category[0] == RIDE_GROUP_ROLLERCOASTER || rideType->category[1] == RIDE_GROUP_ROLLERCOASTER) &&
			ride->status == RIDE_STATUS_OPEN &&
			ride->excitement >= RIDE_RATING(7,00) && type_already_counted[subtype_id] == 0){

			if ((ride_get_total_length(ride) >> 16) > objective_length) {
				type_already_counted[subtype_id]++;
				rcs++;
			}
		}
	}

	if (rcs >= 10)
		scenario_success();
}

static void scenario_objective_check_finish_5_rollercoasters()
{
	int i;
	rct_ride* ride;

	money32 objectiveRideExcitement = gScenarioObjectiveCurrency;

	// ORIGINAL BUG?:
	// This does not check if the rides are even rollercoasters nevermind the right rollercoasters to be finished.
	// It also did not exclude null rides.
	int rcs = 0;
	FOR_ALL_RIDES(i, ride)
		if (ride->status != RIDE_STATUS_CLOSED && ride->excitement >= objectiveRideExcitement)
			rcs++;

	if (rcs >= 5)
		scenario_success();
}

static void scenario_objective_check_replay_loan_and_park_value()
{
	money32 objectiveParkValue = gScenarioObjectiveCurrency;
	money32 parkValue = gParkValue;
	money32 currentLoan = gBankLoan;

	if (currentLoan <= 0 && parkValue >= objectiveParkValue)
		scenario_success();
}

static void scenario_objective_check_monthly_food_income()
{
	money32 *expenditureLastMonth = &gExpenditureTable[1 * RCT_EXPENDITURE_TYPE_COUNT];
	sint32 lastMonthProfit =
		expenditureLastMonth[RCT_EXPENDITURE_TYPE_SHOP_SHOP_SALES] +
		expenditureLastMonth[RCT_EXPENDITURE_TYPE_SHOP_STOCK] +
		expenditureLastMonth[RCT_EXPENDITURE_TYPE_FOODDRINK_SALES] +
		expenditureLastMonth[RCT_EXPENDITURE_TYPE_FOODDRINK_STOCK];

	if (lastMonthProfit >= gScenarioObjectiveCurrency) {
		scenario_success();
	}
}

/**
 * Checks the win/lose conditions of the current objective.
 *  rct2: 0x0066A4B2
 */
static void scenario_objective_check()
{
	if (gScenarioCompletedCompanyValue != MONEY32_UNDEFINED) {
		return;
	}

	switch (gScenarioObjectiveType) {
	case OBJECTIVE_GUESTS_BY:
		scenario_objective_check_guests_by();
		break;
	case OBJECTIVE_PARK_VALUE_BY:
		scenario_objective_check_park_value_by();
		break;
	case OBJECTIVE_10_ROLLERCOASTERS:
		scenario_objective_check_10_rollercoasters();
		break;
	case OBJECTIVE_GUESTS_AND_RATING:
		scenario_objective_check_guests_and_rating();
		break;
	case OBJECTIVE_MONTHLY_RIDE_INCOME:
		scenario_objective_check_monthly_ride_income();
		break;
	case OBJECTIVE_10_ROLLERCOASTERS_LENGTH:
		scenario_objective_check_10_rollercoasters_length();
		break;
	case OBJECTIVE_FINISH_5_ROLLERCOASTERS:
		scenario_objective_check_finish_5_rollercoasters();
		break;
	case OBJECTIVE_REPLAY_LOAN_AND_PARK_VALUE:
		scenario_objective_check_replay_loan_and_park_value();
		break;
	case OBJECTIVE_MONTHLY_FOOD_INCOME:
		scenario_objective_check_monthly_food_income();
		break;
	}
}
