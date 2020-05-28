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

#define TEST_PORT 1235

/* Set below debug_buf to 1 if one wants to debug the buffer sent to 
 * the Guest OS. 
 */
#define debug_buf 0
int start_connection(struct sockaddr_vm sa_listen, int listen_fd, struct sockaddr_vm sa_client, socklen_t socklen_client);
int client_fd;

int get_max_zones() {
	char filename[40] = {0};
	int count = 0;
	
	while(1) {
		sprintf(filename, "/sys/class/thermal/thermal_zone%d", count);
		count++;
		sprintf(filename + strlen(filename), "/type");
		if(!access( filename, R_OK)) {
#if debug_buf		
			printf("Found zone %d \n", count-1);
#endif
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
	char buf[50];
	sprintf(base_path, "/sys/class/thermal/thermal_zone%d", zone_no);
#if debug_buf
	printf("Reading %s/temperature \n", base_path);
#endif
	read_sysfs_values(base_path, TEMPERATURE, &zone->temperature, sizeof(zone->temperature), 1);
#if debug_buf
	printf("Reading %s/type \n", base_path);
#endif
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
	strcpy(head->intelipcid, INTELIPCID);
	head->notifyid = notifyID;
	if (notifyID == 1)
		head->length = maximum_zone_no * sizeof(struct zone_info);
	else if (notifyID == 2)
		head->length = maximum_zone_no * size_temp_type + sizeof(maximum_zone_no);
	else
		printf("Error: NotifyID doesn't match any known packet format\n");
	return;
}

#if debug_buf
int main() {
#else
int send_pkt() {
#endif
	char msgbuf[1024] = {0};
	int maximum_zone_no = 0;
	struct header head;
	int return_value;
	int i = 0;
#if debug_buf
	printf("Starting the thermal utility\n");
#endif
	maximum_zone_no = get_max_zones();
#if debug_buf
	printf("Total number of zones: %d\n\n",maximum_zone_no);
#endif
	struct zone_info zone[maximum_zone_no];
	init_header_struct(&head, maximum_zone_no, 0, 1);
	memcpy(msgbuf, (const unsigned char *)&head, sizeof(head));
	for (i = 0; i < maximum_zone_no; i++) {
#if debug_buf		
		printf("Populating zone_info%d\n", i);
#endif
		populate_zone_info(&zone[i], i);
#if debug_buf
		print_zone_values(zone[i]);
#endif
		memcpy(msgbuf + sizeof(head) + (i * sizeof(struct zone_info)), (const unsigned char *)&zone[i], sizeof(zone[i]));
	}
#if debug_buf
	printf("Sending initial values\n");
#else
	return_value = send(client_fd, msgbuf, sizeof(msgbuf), MSG_DONTWAIT);
	if (return_value == -1)
		goto out;
#endif
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
			sprintf(base_path, "/sys/class/thermal/thermal_zone%d", i);
			memcpy(msgbuf + size_one + (i * size_two), (const unsigned char*)&zone[i].type, sizeof(zone[i].type));
			read_sysfs_values(base_path, TEMPERATURE, &temperature, sizeof(temperature), 1);
			memcpy(msgbuf + size_one + (i * size_two + sizeof(zone[i].type)), (const unsigned char*)&temperature, sizeof(temperature));
		}
#if debug_buf
		printf("Sending values every second\n");
		for (i = 0; i < maximum_zone_no; i++) {
			uint32_t tem = 0;
			uint16_t typ = 0;
			memcpy((unsigned char*)&typ, msgbuf + size_one + (i * size_two), sizeof(typ));
			memcpy((unsigned char*)&tem, msgbuf + size_one + (i * size_two) + sizeof(typ), sizeof(typ));
			printf("Sending temperature %d for type %d for zone %d\n", tem, typ, zone[i].number);
			if (i == 3)
				printf("\n");
		}
#else
		return_value = send(client_fd, msgbuf, sizeof(msgbuf), MSG_DONTWAIT);
		if (return_value == -1)
			goto out;
#endif
	}
	return 0;
out:
	return -1;
}

int start_connection(struct sockaddr_vm sa_listen, int listen_fd, struct sockaddr_vm sa_client, socklen_t socklen_client) {
	int ret;
	fprintf(stderr, "Thermal utility listening on cid(%d), port(%d)\n", sa_listen.svm_cid, sa_listen.svm_port);
	if (listen(listen_fd, 32) != 0) {
		fprintf(stderr, "listen failed\n");
		ret = -1;
		goto out;
	}

	client_fd = accept(listen_fd, (struct sockaddr*)&sa_client, &socklen_client);
	if(client_fd < 0) {
		fprintf(stderr, "accept failed\n");
		ret = -1;
		goto out;
	}
	fprintf(stderr, "Thermal utility connected from guest(%d)\n", sa_client.svm_cid);

	int m_acpidsock;
	struct sockaddr_un m_acpidsockaddr;
	/* Connect to acpid socket */
	m_acpidsock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (m_acpidsock < 0) {
		perror("new acpidsocket failed");
		ret = -2;
		goto out;
	}
		
	m_acpidsockaddr.sun_family = AF_UNIX;
	strcpy(m_acpidsockaddr.sun_path,"/var/run/acpid.socket");
	if(connect(m_acpidsock, (struct sockaddr *)&m_acpidsockaddr, 108)<0)
	{
		/* can't connect */
		perror("connect acpidsocket failed");
		ret = -2;
		goto out;
	}
	goto leave;
out:
	if(listen_fd >= 0)
	{
		printf("Closing listen_fd\n");
		close(listen_fd);
	}

	if(m_acpidsock >= 0)
	{
		printf("Closing acpisocket\n");
		close(m_acpidsock);
	}
leave:
	return ret;
}

#if !debug_buf
int main(int argc, char **argv)
{
	int listen_fd;
	int ret;
	int return_value;

	struct sockaddr_vm sa_listen = {
		.svm_family = AF_VSOCK,
		.svm_cid = VMADDR_CID_ANY,
		.svm_port = TEST_PORT,
	};
	struct sockaddr_vm sa_client;
	socklen_t socklen_client = sizeof(sa_client);

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
	ret = start_connection(sa_listen, listen_fd, sa_client, socklen_client);
	if (ret == -1)
		goto out;
	return_value = send_pkt();
	if (return_value == -1)
		goto start;
out:
	return ret;
}
#endif
