/* 
 * files.c -- DHCP server file manipulation *
 * Rewrite by Russ Dill <Russ.Dill@asu.edu> July 2001
 */
 
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "debug.h"
#include "dhcpd.h"
#include "files.h"
#include "options.h"
#include "leases.h"


/* supported options are easily added here */
struct dhcp_option options[] = {
	/* name[10]	flags				code */
	{"subnet",	OPTION_IP,			0x01},
	{"timezone",	OPTION_S32,			0x02},
	{"router",	OPTION_IP | OPTION_LIST,	0x03},
	{"timesvr",	OPTION_IP | OPTION_LIST,	0x04},
	{"namesvr",	OPTION_IP | OPTION_LIST,	0x05},
	{"dns",		OPTION_IP | OPTION_LIST,	0x06},
	{"logsvr",	OPTION_IP | OPTION_LIST,	0x07},
	{"cookiesvr",	OPTION_IP | OPTION_LIST,	0x08},
	{"lprsvr",	OPTION_IP | OPTION_LIST,	0x09},
	{"bootsize",	OPTION_U16,			0x0d},
	{"domain",	OPTION_STRING,			0x0f},
	{"swapsvr",	OPTION_IP,			0x10},
	{"rootpath",	OPTION_STRING,			0x11},
	{"mtu",		OPTION_U16,			0x1a},
	{"broadcast",	OPTION_IP,			0x1c},
	{"wins",	OPTION_IP | OPTION_LIST,	0x2c},
	{"lease",	OPTION_U32,			0x33},
	{" dhcptype",	OPTION_U8,			0x35}, /* cannot be in config file */
	{" serverid",	OPTION_IP,			0x36}, /* ditto */
	{"ntpsrv",	OPTION_IP | OPTION_LIST,	0x3a},
	{"tftp",	OPTION_STRING,			0x42},
	{"bootfile",	OPTION_STRING,			0x43},
	{"",		0x00,				0x00}
};

/* Lengths of the different option types */
int option_lengths[] = {
	[OPTION_IP] =		4,
	[OPTION_IP_PAIR] =	8,
	[OPTION_BOOLEAN] =	1,
	[OPTION_STRING] =	0,
	[OPTION_U8] =		1,
	[OPTION_U16] =		2,
	[OPTION_S16] =		2,
	[OPTION_U32] =		4,
	[OPTION_S32] =		4
};

/* on these functions, make sure you datatype matches */
static int read_ip(char *line, void *arg)
{
	struct in_addr *addr = arg;
	inet_aton(line, addr);
	return 1;
}

static int read_str(char *line, void *arg)
{
	char **dest = arg;
	
	if (*dest) free(*dest);
	*dest = strdup(line);
	return 1;
}

static int read_u32(char *line, void *arg)
{
	u_int32_t *dest = arg;
	*dest = strtoul(line, NULL, 0);
	return 1;
}

static int read_yn(char *line, void *arg)
{
	char *dest = arg;
	if (!strcasecmp("yes", line) || !strcmp("1", line) || !strcasecmp("true", line))
		*dest = 1;
	else if (!strcasecmp("no", line) || !strcmp("0", line) || !strcasecmp("false", line))
		*dest = 0;
	else return 0;
	
	return 1;
}

/* read a dhcp option and add it to opt_list */
static int read_opt(char *line, void *arg)
{
	struct option_set **opt_list = arg;
	char *opt, *val;
	char fail;
	struct dhcp_option *option = NULL;
	int length = 0;
	char buffer[255];
	u_int16_t result_u16;
	int16_t result_s16;
	u_int16_t result_u32;
	int16_t result_s32;
	
	int i;
	
	if (!(opt = strtok(line, " \t="))) return 0;
	
	for (i = 0; options[i].code; i++)
		if (!strcmp(options[i].name, opt)) {
			option = &(options[i]);
			break;
		}
		
	if (!option) return 0;
	
	do {
		val = strtok(NULL, ", \t");
		if (val) {
			fail = 0;
			length = 0;
			switch (option->flags & TYPE_MASK) {
			case OPTION_IP:
				read_ip(val, buffer);
				break;
			case OPTION_IP_PAIR:
				read_ip(val, buffer);
				if ((val = strtok(NULL, ", \t")))
					read_ip(val, buffer + 4);
				else fail = 1;
				break;
			case OPTION_STRING:
				length = strlen(val);
				if (length > 254) length = 254;
				memcpy(buffer, val, length);
				break;
			case OPTION_BOOLEAN:
				if (!read_yn(val, buffer)) fail = 1;
				break;
			case OPTION_U8:
				buffer[0] = strtoul(val, NULL, 0);
				break;
			case OPTION_U16:
				result_u16 = htons(strtoul(val, NULL, 0));
				memcpy(buffer, &result_u16, 2);
				break;
			case OPTION_S16:
				result_s16 = htons(strtol(val, NULL, 0));
				memcpy(buffer, &result_s16, 2);
				break;
			case OPTION_U32:
				result_u32 = htonl(strtoul(val, NULL, 0));
				memcpy(buffer, &result_u32, 4);
				break;
			case OPTION_S32:
				result_s32 = htonl(strtol(val, NULL, 0));	
				memcpy(buffer, &result_s32, 4);
				break;
			default:
				break;
			}
			length += option_lengths[option->flags & TYPE_MASK];
			if (!fail)
				attach_option(opt_list, option, buffer, length);
		} else fail = 1;
	} while (!fail && option->flags & OPTION_LIST);
	return 1;
}

static struct config_keyword keywords[] = {
	/* keyword	handler   variable address	default */
	{"start",	read_ip,  &(config.start),	"192.168.0.20"},
	{"end",		read_ip,  &(config.end),	"192.168.0.254"},
	{"interface",	read_str, &(config.interface),	"eth0"},
	{"option",	read_opt, &(config.options),	""},
	{"opt",		read_opt, &(config.options),	""},
	{"max_leases",	read_u32, &(config.max_leases),	"254"},
	{"remaining",	read_yn,  &(config.remaining),	"yes"},
	{"auto_time",	read_u32, &(config.auto_time),	"7200"},
	{"decline_time",read_u32, &(config.decline_time),"3600"},
	{"conflict_time",read_u32,&(config.conflict_time),"3600"},
	{"offer_time",	read_u32, &(config.offer_time),	"60"},
	{"min_lease",	read_u32, &(config.min_lease),	"60"},
	{"lease_file",	read_str, &(config.lease_file),	"/etc/udhcpd.leases"},
	{"pid_file",	read_str, &(config.pid_file),	"/var/run/udhcpd.pid"},
	{"notify_file", read_str, &(config.notify_file),""},
	{"",		NULL, NULL, 			""}
};

int read_config(char *file)
{
	FILE *in;
	char buffer[80], *token, *line;
	int i;

	for (i = 0; strlen(keywords[i].keyword); i++)
		if (strlen(keywords[i].def))
			keywords[i].handler(keywords[i].def, keywords[i].var);

	if (!(in = fopen(file, "r"))) {
		DEBUG(LOG_ERR, "unable to open config file: %s", file);
		return 0;
	}
	
	while (fgets(buffer, 80, in)) {
		if (strchr(buffer, '\n')) *(strchr(buffer, '\n')) = '\0';
		if (strchr(buffer, '#')) *(strchr(buffer, '#')) = '\0';
		token = buffer + strspn(buffer, " \t");
		if (*token == '\0') continue;
		line = token + strcspn(token, " \t=");
		if (*line == '\0') continue;
		*line = '\0';
		line++;
		line = line + strspn(line, " \t=");
		if (*line == '\0') continue;
		
		
		
		for (i = 0; strlen(keywords[i].keyword); i++)
			if (!strcasecmp(token, keywords[i].keyword))
				keywords[i].handler(line, keywords[i].var);
	}
	fclose(in);
	return 1;
}

/* the dummy var is here so this can be a signal handler */
void write_leases(int dummy)
{
	FILE *fp;
	unsigned int i;
	char buf[255];
	time_t curr = time(0);
	unsigned long lease_time;
	
	dummy = 0;
	
	if (!(fp = fopen(config.lease_file, "w"))) {
		LOG(LOG_ERR, "Unable to open %s for writing", config.lease_file);
		return;
	}
	
	for (i = 0; i < config.max_leases; i++) {
		if (leases[i].yiaddr != 0) {
			if (config.remaining) {
				if (lease_expired(&(leases[i])))
					lease_time = 0;
				else lease_time = leases[i].expires - curr;
			} else lease_time = leases[i].expires;
			lease_time = htonl(lease_time);
			fwrite(leases[i].chaddr, 16, 1, fp);
			fwrite(&(leases[i].yiaddr), 4, 1, fp);
			fwrite(&lease_time, 4, 1, fp);
		}
	}
	fclose(fp);
	
	if (strlen(config.notify_file)) {
		sprintf(buf, "%s %s", config.notify_file, config.lease_file);
		system(buf);
	}
}

void read_leases(char *file)
{
	FILE *fp;
	unsigned int i = 0;
	time_t curr = time(0);
	struct dhcpOfferedAddr lease;
	
	if (!(fp = fopen(file, "r"))) {
		LOG(LOG_ERR, "Unable to open %s for reading", file);
		return;
	}
	
	while (i < config.max_leases && (fread(&lease, sizeof lease, 1, fp) == 1)) {
		if (lease.yiaddr >= config.start && lease.yiaddr <= config.end) {
			leases[i].yiaddr = lease.yiaddr;
			leases[i].expires = ntohl(lease.expires);	
			if (config.remaining) leases[i].expires += curr;
			memcpy(leases[i].chaddr, lease.chaddr, sizeof(lease.chaddr));
			i++;
		}
	}
	
	DEBUG(LOG_INFO, "Read %d leases", i);
	
	if (i == config.max_leases) {
		if (fgetc(fp) == EOF)
			/* might be helpfull to drop expired leases */
			LOG(LOG_WARNING, "Too many leases while loading %s\n", file);
	}
	fclose(fp);
}
		
		
