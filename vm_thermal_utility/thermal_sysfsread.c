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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <linux/vm_sockets.h>
#include <sys/un.h>
#include "thermal_pkt.h"

#define TEST_PORT 14096

int start_connection(struct sockaddr_vm sa_listen, int listen_fd, socklen_t socklen_client);
int client_fd = 0;

int get_max_zones() {
	char filename[45] = {0};
	int count = 0;

	while(1) {
		snprintf(filename, sizeof(filename), "/sys/class/thermal/thermal_zone%d", count);
		count++;
		snprintf(filename + strlen(filename), sizeof(filename), "/type");
		if(!access( filename, R_OK)) {
			continue;
		}
		else
			break;
	}

	return count-1;
}

/* read_sysfs_values: Function to read the filename sysfs value in thermal_zones
 * module.
 */
void read_sysfs_values(char *base_path, char *filename, void *buf, int len, int flag)
{
	char sysfs_path[120];

	snprintf(sysfs_path, 120, "%s%s", base_path, "/");
	snprintf(sysfs_path + strlen(sysfs_path), 120 - strlen(sysfs_path), "%s", filename);

	FILE *fp = fopen(sysfs_path, "r");
	if (!fp) {  /* validate file open for reading */
		fprintf (stderr, "Failed to open file for read.\n");
		return;
	}

	if (flag==0)
		fread(buf, len, 1, fp);
	else
    		fscanf (fp, "%d", (int*)buf);  /* read/validate value */
	fclose (fp);
	return;
}

/* populate_zone_info: Function to populate the zone_info struture with all
 * the values for the zone number passed as variable here.
 */

void populate_zone_info (struct zone_info *zone, int zone_no) {
	char base_path[120] = "/sys/class/thermal/thermal_zone";
	char buf[50] = {0};
	snprintf(base_path, sizeof(base_path), "/sys/class/thermal/thermal_zone%d", zone_no);
	read_sysfs_values(base_path, TEMPERATURE, &zone->temperature, sizeof(zone->temperature), 1);
	read_sysfs_values(base_path, TYPE, buf, 50, 0);
	zone->number = zone_no;
	if (strstr(buf, "x86_pkg_temp") != NULL) {
		zone->type = CPU;
		zone->trip_0 = CPU_TRIP_0;
		zone->trip_1 = CPU_TRIP_1;
		zone->trip_2 = CPU_TRIP_2;
		return;
	} else if (strstr(buf, "battery") != NULL) {
		zone->type = BATTERY;
		zone->trip_0 = zone->trip_1 = zone->trip_2 = UNKNOWN_TRIP;
		return;
	} else {
		zone->type = UNKNOWN_TYPE;
		zone->trip_0 = zone->trip_1 = zone->trip_2 = UNKNOWN_TRIP;
		return;
	}
	return;
}

/* printf_zone_values: Print all the struture values for the
 * structure variable passed
 */

void print_zone_values(struct zone_info zone) {
	printf("Zone Number: %d\n", zone.number);
	printf("Zone Type: %d\n", zone.type);
	printf("Zone Temperature: %d\n", zone.temperature);
	printf("Zone Trip Point 0: %d\n", zone.trip_0);
	printf("Zone Trip Point 1: %d\n", zone.trip_1);
	printf("Zone Trip Point 2: %d\n", zone.trip_2);
	printf("\n");
	return;
}

/* init_header_struct: Function to initialize the header struture with proper values.
 * This works for both types of notification packets. (NotifyID = 1 || 2)
 */

void init_header_struct(struct header *head, uint32_t maximum_zone_no, int size_temp_type, uint16_t notifyID) {
	strncpy((char *)head->intelipcid, INTELIPCID, sizeof(head->intelipcid));
	head->notifyid = notifyID;
	if (notifyID == 1)
		head->length = maximum_zone_no * sizeof(struct zone_info);
	else if (notifyID == 2)
		head->length = maximum_zone_no * size_temp_type + sizeof(maximum_zone_no);
	else
		printf("Error: NotifyID doesn't match any known packet format\n");
	return;
}

int send_pkt() {
	char msgbuf[1024] = {0};
	int maximum_zone_no = 0;
	struct header head;
	int return_value = 0;
	int i = 0;
	maximum_zone_no = get_max_zones();
	struct zone_info zone[maximum_zone_no];
	init_header_struct(&head, maximum_zone_no, 0, 1);
	memcpy(msgbuf, (const unsigned char *)&head, sizeof(head));
	for (i = 0; i < maximum_zone_no; i++) {
		populate_zone_info(&zone[i], i);
		memcpy(msgbuf + sizeof(head) + (i * sizeof(struct zone_info)), (const unsigned char *)&zone[i], sizeof(zone[i]));
	}
	return_value = send(client_fd, msgbuf, sizeof(msgbuf), MSG_DONTWAIT);
	if (return_value == -1)
		goto out;
	init_header_struct(&head, maximum_zone_no, sizeof(zone[i].temperature) + sizeof(zone[i].type), 2);
	char base_path[120] = "/sys/class/thermal/thermal_zone";
	int size_one = sizeof(head) + sizeof(maximum_zone_no);
	int size_two = sizeof(zone[0].type) + sizeof(zone[0].temperature);

	while (1) {

		sleep(1);
		memcpy(msgbuf, (const unsigned char *)&head, sizeof(head));

/* TODO As of now we are sending the updated values for all the thermal zones
 * But in future we will be sending the values of only the zones where there is
 * an update in the temperature values. So below memcpy needs to be implemented
 * properly with that implementation
 */
		memcpy(msgbuf + sizeof(head), (const unsigned char *)&maximum_zone_no, sizeof(maximum_zone_no));
		uint32_t temperature = 0;
		for (i = 0; i < maximum_zone_no; i++) {
			snprintf(base_path, sizeof(base_path), "/sys/class/thermal/thermal_zone%d", i);
			memcpy(msgbuf + size_one + (i * size_two), (const unsigned char*)&zone[i].type, sizeof(zone[i].type));
			read_sysfs_values(base_path, TEMPERATURE, &temperature, sizeof(temperature), 1);
			memcpy(msgbuf + size_one + (i * size_two + sizeof(zone[i].type)), (const unsigned char*)&temperature, sizeof(temperature));
		}
		return_value = send(client_fd, msgbuf, sizeof(msgbuf), MSG_DONTWAIT);
		if (return_value == -1)
			goto out;
	}
	return 0;
out:
	return -1;
}

int start_connection(struct sockaddr_vm sa_listen, int listen_fd, socklen_t socklen_client) {
	struct sockaddr_vm sa_client;
	fprintf(stderr, "Thermal utility listening on cid(%d), port(%d)\n", sa_listen.svm_cid, sa_listen.svm_port);
	if (listen(listen_fd, 32) != 0) {
		fprintf(stderr, "listen failed\n");
		return -1;
	}

	client_fd = accept(listen_fd, (struct sockaddr*)&sa_client, &socklen_client);
	if(client_fd < 0) {
		fprintf(stderr, "accept failed\n");
		return -1;
	}
	fprintf(stderr, "Thermal utility connected from guest(%d)\n", sa_client.svm_cid);

	return 0;
}

int main()
{
	int listen_fd = 0;
	int ret = 0;
	int return_value = 0;

	struct sockaddr_vm sa_listen = {
		.svm_family = AF_VSOCK,
		.svm_cid = VMADDR_CID_ANY,
		.svm_port = TEST_PORT,
	};
	socklen_t socklen_client = sizeof(struct sockaddr_vm);

	listen_fd = socket(AF_VSOCK, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		fprintf(stderr, "socket init failed\n");
		ret = -1;
		goto out;
	}

	if (bind(listen_fd, (struct sockaddr*)&sa_listen, sizeof(sa_listen)) != 0) {
		perror("bind failed");
		ret = -1;
		goto out;
	}

start:
	ret = start_connection(sa_listen, listen_fd, socklen_client);
	if (ret < 0)
		goto out;
	return_value = send_pkt();
	if (return_value == -1)
		goto start;
out:
	if(listen_fd >= 0)
	{
		printf("Closing listen_fd\n");
		close(listen_fd);
	}

	return ret;
}
