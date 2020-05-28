/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>

#define INTELIPCID "INTELIPC"

#define CPU 0
#define GPU 1
#define BATTERY 2
#define SKIN 3
#define UNKNOWN_TYPE 65535

#define TEMPERATURE "temp"
#define TYPE "type"

#define CPU_TRIP_0 85000
#define CPU_TRIP_1 95000
#define CPU_TRIP_2 99000
#define UNKNOWN_TRIP -1

struct zone_info {
	uint32_t temperature;
	uint32_t trip_0;
	uint32_t trip_1;
	uint32_t trip_2;
	uint16_t number;
	uint16_t type;
};

struct header {
	uint8_t intelipcid[8];
	uint16_t notifyid;
	uint16_t length;
};

