/*
 * OpenTyrian Classic: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "config.h"
#include "file.h"
#include "joystick.h"
#include "keyboard.h"
#include "nortsong.h"
#include "opentyr.h"
#include "params.h"
#include "varz.h"
#include "video.h"
#include <cJSON.h>
#include <assert.h>

int joystick_axis_threshold( int j, int value );
int check_assigned( SDL_Joystick *joystick_handle, const Joystick_assignment assignment[2] );

const char *assignment_to_code( const Joystick_assignment *assignment );
void code_to_assignment( Joystick_assignment *assignment, const char *buffer );

int joystick_repeat_delay = 300; // milliseconds, repeat delay for buttons
bool joydown = false;            // any joystick buttons down, updated by poll_joysticks()
bool ignore_joystick = true;

int joysticks = 0;
Joystick *joystick = NULL;

#ifdef __BLACKBERRY__
#else
static const char joystick_cfg_version = 1;
static const int joystick_analog_max = 32767;
#endif

// eliminates axis movement below the threshold
int joystick_axis_threshold( int j, int value )
{
	assert(j < joysticks);

	bool negative = value < 0;
	if (negative)
		value = -value;

	if (value <= joystick[j].threshold * 1000)
		return 0;

	value -= joystick[j].threshold * 1000;

	return negative ? -value : value;
}

// converts joystick axis to sane Tyrian-usable value (based on sensitivity)
int joystick_axis_reduce( int j, int value )
{
	assert(j < joysticks);

	value = joystick_axis_threshold(j, value);

	if (value == 0)
		return 0;

	return value / (3000 - 200 * joystick[j].sensitivity);
}

// converts analog joystick axes to an angle
// returns false if axes are centered (there is no angle)
bool joystick_analog_angle( int j, float *angle )
{
#ifdef __BLACKBERRY__
#else
	assert(j < joysticks);

	float x = joystick_axis_threshold(j, joystick[j].x), y = joystick_axis_threshold(j, joystick[j].y);

	if (x != 0)
	{
		*angle += atanf(-y / x);
		*angle += (x < 0) ? -M_PI_2 : M_PI_2;
		return true;
	}
	else if (y != 0)
	{
		*angle += y < 0 ? M_PI : 0;
		return true;
	}
#endif
	return false;
}

/* gives back value 0..joystick_analog_max indicating that one of the assigned
 * buttons has been pressed or that one of the assigned axes/hats has been moved
 * in the assigned direction
 */
int check_assigned( SDL_Joystick *joystick_handle, const Joystick_assignment assignment[2] )
{
	int result = 0;
#ifdef __BLACKBERRY__
#else
	for (int i = 0; i < 2; i++)
	{
		int temp = 0;

		switch (assignment[i].type)
		{
		case NONE:
			continue;

		case AXIS:
			temp = SDL_JoystickGetAxis(joystick_handle, assignment[i].num);

			if (assignment[i].negative_axis)
				temp = -temp;
			break;

		case BUTTON:
			temp = SDL_JoystickGetButton(joystick_handle, assignment[i].num) == 1 ? joystick_analog_max : 0;
			break;

		case HAT:
			temp = SDL_JoystickGetHat(joystick_handle, assignment[i].num);

			if (assignment[i].x_axis)
				temp &= SDL_HAT_LEFT | SDL_HAT_RIGHT;
			else
				temp &= SDL_HAT_UP | SDL_HAT_DOWN;

			if (assignment[i].negative_axis)
				temp &= SDL_HAT_LEFT | SDL_HAT_UP;
			else
				temp &= SDL_HAT_RIGHT | SDL_HAT_DOWN;

			temp = temp ? joystick_analog_max : 0;
			break;
		}

		if (temp > result)
			result = temp;
	}
#endif
	return result;
}

// updates joystick state
void poll_joystick( int j )
{
#ifdef __BLACKBERRY__
#else
	assert(j < joysticks);

	if (joystick[j].handle == NULL)
		return;

	SDL_JoystickUpdate();

	// indicates that a direction/action was pressed since last poll
	joystick[j].input_pressed = false;

	// indicates that an direction/action has been held long enough to fake a repeat press
	bool repeat = joystick[j].joystick_delay < SDL_GetTicks();

	// update direction state
	for (uint d = 0; d < COUNTOF(joystick[j].direction); d++)
	{
		bool old = joystick[j].direction[d];

		joystick[j].analog_direction[d] = check_assigned(joystick[j].handle, joystick[j].assignment[d]);
		joystick[j].direction[d] = joystick[j].analog_direction[d] > (joystick_analog_max / 2);
		joydown |= joystick[j].direction[d];

		joystick[j].direction_pressed[d] = joystick[j].direction[d] && (!old || repeat);
		joystick[j].input_pressed |= joystick[j].direction_pressed[d];
	}

	joystick[j].x = -joystick[j].analog_direction[3] + joystick[j].analog_direction[1];
	joystick[j].y = -joystick[j].analog_direction[0] + joystick[j].analog_direction[2];

	// update action state
	for (uint d = 0; d < COUNTOF(joystick[j].action); d++)
	{
		bool old = joystick[j].action[d];

		joystick[j].action[d] = check_assigned(joystick[j].handle, joystick[j].assignment[d + COUNTOF(joystick[j].direction)]);
		joydown |= joystick[j].action[d];

		joystick[j].action_pressed[d] = joystick[j].action[d] && (!old || repeat);
		joystick[j].input_pressed |= joystick[j].action_pressed[d];
	}

	joystick[j].confirm = joystick[j].action[0] || joystick[j].action[4];
	joystick[j].cancel = joystick[j].action[1] || joystick[j].action[5];

	// if new input, reset press-repeat delay
	if (joystick[j].input_pressed)
		joystick[j].joystick_delay = SDL_GetTicks() + joystick_repeat_delay;
#endif
}

// updates all joystick states
void poll_joysticks( void )
{
#ifdef __BLACKBERRY__
#else
	joydown = false;

	for (int j = 0; j < joysticks; j++)
		poll_joystick(j);
#endif
}

// sends SDL KEYDOWN and KEYUP events for a key
void push_key( SDLKey key )
{
	SDL_Event e;

	memset(&e.key.keysym, 0, sizeof(e.key.keysym));

	e.key.keysym.sym = key;
	e.key.keysym.unicode = key;

	e.key.state = SDL_RELEASED;

	e.type = SDL_KEYDOWN;
	SDL_PushEvent(&e);

	e.type = SDL_KEYUP;
	SDL_PushEvent(&e);
}

// helps us be lazy by pretending joysticks are a keyboard (useful for menus)
void push_joysticks_as_keyboard( void )
{
#ifdef __BLACKBERRY__
#else
	const SDLKey confirm = SDLK_RETURN, cancel = SDLK_ESCAPE;
	const SDLKey direction[4] = { SDLK_UP, SDLK_RIGHT, SDLK_DOWN, SDLK_LEFT };

	poll_joysticks();

	for (int j = 0; j < joysticks; j++)
	{
		if (!joystick[j].input_pressed)
			continue;

		if (joystick[j].confirm)
			push_key(confirm);
		if (joystick[j].cancel)
			push_key(cancel);

		for (uint d = 0; d < COUNTOF(joystick[j].direction_pressed); d++)
		{
			if (joystick[j].direction_pressed[d])
				push_key(direction[d]);
		}
	}
#endif
}

// initializes SDL joystick system and loads assignments for joysticks found
void init_joysticks( void )
{
	if (ignore_joystick)
		return;
#ifdef __BLACKBERRY__
#else
	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK))
	{
		fprintf(stderr, "warning: failed to initialize joystick system: %s\n", SDL_GetError());
		ignore_joystick = true;
		return;
	}

	SDL_JoystickEventState(SDL_IGNORE);

	joysticks = SDL_NumJoysticks();
	joystick = malloc(joysticks * sizeof(*joystick));

	for (int j = 0; j < joysticks; j++)
	{
		memset(&joystick[j], 0, sizeof(*joystick));

		joystick[j].handle = SDL_JoystickOpen(j);
		if (joystick[j].handle != NULL)
		{
			printf("joystick detected: %s ", SDL_JoystickName(j));
			printf("(%d axes, %d buttons, %d hats)\n",
			       SDL_JoystickNumAxes(joystick[j].handle),
			       SDL_JoystickNumButtons(joystick[j].handle),
			       SDL_JoystickNumHats(joystick[j].handle));

			if (!load_joystick_assignments(j))
				reset_joystick_assignments(j);
		}
	}

	if (joysticks == 0)
		printf("no joysticks detected\n");
#endif /* __BLACKBERRY__ */
}

// deinitializes SDL joystick system and saves joystick assignments
void deinit_joysticks( void )
{
	if (ignore_joystick)
		return;
#ifdef __BLACKBERRY__
#else
	for (int j = 0; j < joysticks; j++)
	{
		if (joystick[j].handle != NULL)
		{
			save_joystick_assignments(j);
			SDL_JoystickClose(joystick[j].handle);
		}
	}

	free(joystick);

	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
#endif /* __BLACKBERRY__ */
}

void reset_joystick_assignments( int j )
{
#ifdef __BLACKBERRY__
#else

	assert(j < joysticks);

	// defaults: first 2 axes, first hat, first 6 buttons
	for (uint a = 0; a < COUNTOF(joystick[j].assignment); a++)
	{
		// clear assignments
		for (uint i = 0; i < COUNTOF(joystick[j].assignment[a]); i++)
			joystick[j].assignment[a][i].type = NONE;

		if (a < 4)
		{
			if (SDL_JoystickNumAxes(joystick[j].handle) >= 2)
			{
				joystick[j].assignment[a][0].type = AXIS;
				joystick[j].assignment[a][0].num = (a + 1) % 2;
				joystick[j].assignment[a][0].negative_axis = (a == 0 || a == 3);
			}

			if (SDL_JoystickNumHats(joystick[j].handle) >= 1)
			{
				joystick[j].assignment[a][1].type = HAT;
				joystick[j].assignment[a][1].num = 0;
				joystick[j].assignment[a][1].x_axis = (a == 1 || a == 3);
				joystick[j].assignment[a][1].negative_axis = (a == 0 || a == 3);
			}
		}
		else
		{
			if (a - 4 < (unsigned)SDL_JoystickNumButtons(joystick[j].handle))
			{
				joystick[j].assignment[a][0].type = BUTTON;
				joystick[j].assignment[a][0].num = a - 4;
			}
		}
	}

	joystick[j].analog = false;
	joystick[j].sensitivity = 5;
	joystick[j].threshold = 5;
#endif /* __BLACKBERRY__ */
}

bool load_joystick_assignments( int j )
{
#ifdef __BLACKBERRY__
#else
	cJSON *root = load_json("joystick.conf");
	if (root == NULL)
		return false;

	cJSON *config = cJSON_GetObjectItem(root, SDL_JoystickName(j));
	if (config == NULL)
	{
		cJSON_Delete(root);
		return false;
	}

	cJSON *setting;

	if ((setting = cJSON_GetObjectItem(config, "analog")))
		joystick[j].analog = (setting->type == cJSON_True);

	if ((setting = cJSON_GetObjectItem(config, "sensitivity")))
		joystick[j].sensitivity = setting->valueint;

	if ((setting = cJSON_GetObjectItem(config, "threshold")))
		joystick[j].threshold = setting->valueint;

	if ((setting = cJSON_GetObjectItem(config, "assignments")))
	{
		for (uint i = 0; i < COUNTOF(joystick->assignment); ++i)
		{
			cJSON *assignments = cJSON_GetArrayItem(setting, i);
			if (assignments == NULL)
				break;

			for (uint k = 0; k < COUNTOF(*joystick->assignment); ++k)
			{
				cJSON *assignment = cJSON_GetArrayItem(assignments, k);
				if (assignment)
					code_to_assignment(&joystick[j].assignment[i][k], assignment->valuestring);
			}
		}
	}

	cJSON_Delete(root);
#endif /* __BLACKBERRY__ */
	return true;
}

bool save_joystick_assignments( int j )
{
#ifdef __BLACKBERRY__
#else
	cJSON *root = load_json("joystick.conf");
	if (root == NULL)
		root = cJSON_CreateObject();

	cJSON *config = cJSON_CreateOrGetObjectItem(root, SDL_JoystickName(j));
	cJSON_ForceType(config, cJSON_Object);

	cJSON *setting;

	setting = cJSON_CreateOrGetObjectItem(config, "analog");
	cJSON_SetBoolean(setting, joystick[j].analog);

	setting = cJSON_CreateOrGetObjectItem(config, "sensitivity");
	cJSON_SetNumber(setting, joystick[j].sensitivity);

	setting = cJSON_CreateOrGetObjectItem(config, "threshold");
	cJSON_SetNumber(setting, joystick[j].threshold);

	setting = cJSON_CreateOrGetObjectItem(config, "assignments");
	cJSON_ForceType(setting, cJSON_Array);
	cJSON_ClearArray(setting);

	for (uint i = 0; i < COUNTOF(joystick->assignment); ++i)
	{
		cJSON *assignments;
		cJSON_AddItemToArray(setting, assignments = cJSON_CreateArray());

		for (uint k = 0; k < COUNTOF(*joystick->assignment); ++k)
		{
			if (joystick[j].assignment[i][k].type == NONE)
				continue;

			cJSON_AddItemToArray(assignments, cJSON_CreateString(assignment_to_code(&joystick[j].assignment[i][k])));
		}
	}

	save_json(root, "joystick.conf");

	cJSON_Delete(root);
#endif /* __BLACKBERRY__ */
	return true;
}

// fills buffer with comma separated list of assigned joystick functions
void joystick_assignments_to_string( char *buffer, size_t buffer_len, const Joystick_assignment *assignments )
{
#ifdef __BLACKBERRY__
#else
	strncpy(buffer, "", buffer_len);

	bool comma = false;
	for (uint i = 0; i < COUNTOF(*joystick->assignment); ++i)
	{
		if (assignments[i].type == NONE)
			continue;

		size_t len = snprintf(buffer, buffer_len, "%s%s",
		                      comma ? ", " : "",
		                      assignment_to_code(&assignments[i]));
		buffer += len;
		buffer_len -= len;

		comma = true;
	}
#endif /* __BLACKBERRY__ */
}

// reverse of assignment_to_code()
void code_to_assignment( Joystick_assignment *assignment, const char *buffer )
{
	memset(assignment, 0, sizeof(*assignment));

	char axis = 0, direction = 0;

	if (sscanf(buffer, " AX %d%c", &assignment->num, &direction) == 2)
		assignment->type = AXIS;
	else if (sscanf(buffer, " BTN %d", &assignment->num) == 1)
		assignment->type = BUTTON;
	else if (sscanf(buffer, " H %d%c%c", &assignment->num, &axis, &direction) == 3)
		assignment->type = HAT;

	if (assignment->num == 0)
		assignment->type = NONE;
	else
		--assignment->num;

	assignment->x_axis = (toupper(axis) == 'X');
	assignment->negative_axis = (toupper(direction) == '-');
}

/* gives the short (6 or less characters) identifier for a joystick assignment
 *
 * two of these per direction/action is all that can fit on the joystick config screen,
 * assuming two digits for the axis/button/hat number
 */
const char *assignment_to_code( const Joystick_assignment *assignment )
{
	static char name[7];

	switch (assignment->type)
	{
	case NONE:
		strcpy(name, "");
		break;

	case AXIS:
		snprintf(name, sizeof(name), "AX %d%c",
		         assignment->num + 1,
		         assignment->negative_axis ? '-' : '+');
		break;

	case BUTTON:
		snprintf(name, sizeof(name), "BTN %d",
		         assignment->num + 1);
		break;

	case HAT:
		snprintf(name, sizeof(name), "H %d%c%c",
		         assignment->num + 1,
		         assignment->x_axis ? 'X' : 'Y',
		         assignment->negative_axis ? '-' : '+');
		break;
	}

	return name;
}

// captures joystick input for configuring assignments
// returns false if non-joystick input was detected
// TODO: input from joystick other than the one being configured probably should not be ignored
bool detect_joystick_assignment( int j, Joystick_assignment *assignment )
{
#ifdef __BLACKBERRY__
	bool detected = false;
#else

	// get initial joystick state to compare against to see if anything was pressed

	const int axes = SDL_JoystickNumAxes(joystick[j].handle);
	Sint16 *axis = malloc(axes * sizeof(*axis));
	for (int i = 0; i < axes; i++)
		axis[i] = SDL_JoystickGetAxis(joystick[j].handle, i);

	const int buttons = SDL_JoystickNumButtons(joystick[j].handle);
	Uint8 *button = malloc(buttons * sizeof(*button));
	for (int i = 0; i < buttons; i++)
		button[i] = SDL_JoystickGetButton(joystick[j].handle, i);

	const int hats = SDL_JoystickNumHats(joystick[j].handle);
	Uint8 *hat = malloc(hats * sizeof(*hat));
	for (int i = 0; i < hats; i++)
		hat[i] = SDL_JoystickGetHat(joystick[j].handle, i);

	bool detected = false;

	do
	{
		setjasondelay(1);

		SDL_JoystickUpdate();

		for (int i = 0; i < axes; ++i)
		{
			Sint16 temp = SDL_JoystickGetAxis(joystick[j].handle, i);

			if (abs(temp - axis[i]) > joystick_analog_max * 2 / 3)
			{
				assignment->type = AXIS;
				assignment->num = i;
				assignment->negative_axis = temp < axis[i];
				detected = true;
				break;
			}
		}

		for (int i = 0; i < buttons; ++i)
		{
			Uint8 new_button = SDL_JoystickGetButton(joystick[j].handle, i),
			      changed = button[i] ^ new_button;

			if (!changed)
				continue;

			if (new_button == 0) // button was released
			{
				button[i] = new_button;
			}
			else                 // button was pressed
			{
				assignment->type = BUTTON;
				assignment->num = i;
				detected = true;
				break;
			}
		}

		for (int i = 0; i < hats; ++i)
		{
			Uint8 new_hat = SDL_JoystickGetHat(joystick[j].handle, i),
			      changed = hat[i] ^ new_hat;

			if (!changed)
				continue;

			if ((new_hat & changed) == SDL_HAT_CENTERED) // hat was centered
			{
				hat[i] = new_hat;
			}
			else
			{
				assignment->type = HAT;
				assignment->num = i;
				assignment->x_axis = changed & (SDL_HAT_LEFT | SDL_HAT_RIGHT);
				assignment->negative_axis = changed & (SDL_HAT_LEFT | SDL_HAT_UP);
				detected = true;
			}
		}

		service_SDL_events(true);
		JE_showVGA();

		wait_delay();
	}
	while (!detected && !newkey && !newmouse);

	free(axis);
	free(button);
	free(hat);
#endif
	return detected;
}

// compares relevant parts of joystick assignments for equality
bool joystick_assignment_cmp( const Joystick_assignment *a, const Joystick_assignment *b )
{
#ifdef __BLACKBERRY__
#else

	if (a->type == b->type)
	{
		switch (a->type)
		{
		case NONE:
			return true;
		case AXIS:
			return (a->num == b->num) &&
			       (a->negative_axis == b->negative_axis);
		case BUTTON:
			return (a->num == b->num);
		case HAT:
			return (a->num == b->num) &&
			       (a->x_axis == b->x_axis) &&
			       (a->negative_axis == b->negative_axis);
		}
	}
#endif
	return false;
}
