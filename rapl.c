/* Run a program and read its energy                                  */
/*                                                                    */
/*  Example:                                                          */
/*     $ rapl-run sleep 1                                             */
/*       ...                                                          */
/*       domain, energy, time, avg_power                              */
/*       energy-cores, 7.243286, 2.000902, 3.620010                   */
/*       energy-gpu, 1.565857, 2.000902, 0.782575                     */
/*       energy-pkg, 16.280029, 2.000902, 8.136344                    */
/*       energy-ram, 4.357971, 2.000902, 2.178003                     */
/*                                                                    */
/* Code modified from:                                                */
/*    http://web.eece.maine.edu/~vweaver/projects/rapl/rapl-read.c    */
/* Code based on Intel RAPL driver by Zhang Rui <rui.zhang@intel.com> */
/*                                                                    */
/* Code to properly get this info from Linux through a real device    */
/*   driver and the perf tool should be available as of Linux 3.14    */
/* Compile with:   gcc -O2 -Wall -o rapl-run rapl-run.c -lm           */
/*                                                                    */
/* Vince Weaver -- vincent.weaver @ maine.edu -- 16 June 2015         */
/*                                                                    */
/* Additional contributions by:                                       */
/*   Romain Dolbeau -- romain @ dolbeau.org                           */
/*   James Pallister-- james @ jpallister.com                         */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <string.h>

#include <sys/syscall.h>
#include <linux/perf_event.h>

#define MSR_RAPL_POWER_UNIT		0x606

/*
 * Platform specific RAPL Domains.
 * Note that PP1 RAPL Domain is supported on 062A only
 * And DRAM RAPL Domain is supported on 062D only
 */
/* Package RAPL Domain */
#define MSR_PKG_RAPL_POWER_LIMIT	0x610
#define MSR_PKG_ENERGY_STATUS		0x611
#define MSR_PKG_PERF_STATUS		0x613
#define MSR_PKG_POWER_INFO		0x614

/* PP0 RAPL Domain */
#define MSR_PP0_POWER_LIMIT		0x638
#define MSR_PP0_ENERGY_STATUS		0x639
#define MSR_PP0_POLICY			0x63A
#define MSR_PP0_PERF_STATUS		0x63B

/* PP1 RAPL Domain, may reflect to uncore devices */
#define MSR_PP1_POWER_LIMIT		0x640
#define MSR_PP1_ENERGY_STATUS		0x641
#define MSR_PP1_POLICY			0x642

/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT		0x618
#define MSR_DRAM_ENERGY_STATUS		0x619
#define MSR_DRAM_PERF_STATUS		0x61B
#define MSR_DRAM_POWER_INFO		0x61C

/* RAPL UNIT BITMASK */
#define POWER_UNIT_OFFSET	0
#define POWER_UNIT_MASK		0x0F

#define ENERGY_UNIT_OFFSET	0x08
#define ENERGY_UNIT_MASK	0x1F00

#define TIME_UNIT_OFFSET	0x10
#define TIME_UNIT_MASK		0xF000

static int open_msr(int core) {

  char msr_filename[BUFSIZ];
  int fd;

  sprintf(msr_filename, "/dev/cpu/%d/msr", core);
  fd = open(msr_filename, O_RDONLY);
  if ( fd < 0 ) {
    if ( errno == ENXIO ) {
      fprintf(stderr, "rdmsr: No CPU %d\n", core);
      exit(2);
    } else if ( errno == EIO ) {
      fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n", core);
      exit(3);
    } else {
      perror("rdmsr:open");
      fprintf(stderr,"Trying to open %s\n",msr_filename);
      exit(127);
    }
  }

  return fd;
}

static long long read_msr(int fd, int which) {

  uint64_t data;

  if ( pread(fd, &data, sizeof data, which) != sizeof data ) {
    perror("rdmsr:pread");
    exit(127);
  }

  return (long long)data;
}

#define CPU_SANDYBRIDGE		42
#define CPU_SANDYBRIDGE_EP	45
#define CPU_IVYBRIDGE		58
#define CPU_IVYBRIDGE_EP	62
#define CPU_HASWELL		60
#define CPU_HASWELL_EP		63
#define CPU_BROADWELL		61



static int detect_cpu(void) {

	FILE *fff;

	int family,model=-1;
	char buffer[BUFSIZ],*result;
	char vendor[BUFSIZ];

	fff=fopen("/proc/cpuinfo","r");
	if (fff==NULL) return -1;

	while(1) {
		result=fgets(buffer,BUFSIZ,fff);
		if (result==NULL) break;

		if (!strncmp(result,"vendor_id",8)) {
			sscanf(result,"%*s%*s%s",vendor);

			if (strncmp(vendor,"GenuineIntel",12)) {
				printf("%s not an Intel chip\n",vendor);
				return -1;
			}
		}

		if (!strncmp(result,"cpu family",10)) {
			sscanf(result,"%*s%*s%*s%d",&family);
			if (family!=6) {
				printf("Wrong CPU family %d\n",family);
				return -1;
			}
		}

		if (!strncmp(result,"model",5)) {
			sscanf(result,"%*s%*s%d",&model);
		}

	}

	fclose(fff);

	switch(model) {
		case CPU_SANDYBRIDGE:
			printf("Found Sandybridge CPU\n");
			break;
		case CPU_SANDYBRIDGE_EP:
			printf("Found Sandybridge-EP CPU\n");
			break;
		case CPU_IVYBRIDGE:
			printf("Found Ivybridge CPU\n");
			break;
		case CPU_IVYBRIDGE_EP:
			printf("Found Ivybridge-EP CPU\n");
			break;
		case CPU_HASWELL:
			printf("Found Haswell CPU\n");
			break;
		case CPU_HASWELL_EP:
			printf("Found Haswell-EP CPU\n");
			break;
		case CPU_BROADWELL:
			printf("Found Broadwell CPU\n");
			break;
		default:	printf("Unsupported model %d\n",model);
				model=-1;
				break;
	}

	return model;
}


static int perf_event_open(struct perf_event_attr *hw_event_uptr,
                    pid_t pid, int cpu, int group_fd, unsigned long flags) {

        return syscall(__NR_perf_event_open,hw_event_uptr, pid, cpu,
                        group_fd, flags);
}

#define NUM_RAPL_DOMAINS	4

char rapl_domain_names[NUM_RAPL_DOMAINS][30]= {
	"energy-cores",
	"energy-gpu",
	"energy-pkg",
	"energy-ram",
};

struct timespec diff(struct timespec start, struct timespec end)
{
	struct timespec temp;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp;
}

static int rapl_perf(int core, float t) {

	FILE *fff;
	int type;
	int config=0;
	char units[BUFSIZ];
	char filename[BUFSIZ];
	int fd[NUM_RAPL_DOMAINS];
	double scale[NUM_RAPL_DOMAINS];
	struct perf_event_attr attr;
	long long value;
	int i;

	fff=fopen("/sys/bus/event_source/devices/power/type","r");
	if (fff==NULL) {
		printf("No perf_event rapl support found (requires Linux 3.14)\n");
		printf("Falling back to raw msr support\n");
		return -1;
	}
	fscanf(fff,"%d",&type);
	fclose(fff);

	// printf("Using perf_event to gather RAPL results\n");
	// printf("For raw msr results use the -m option\n\n");

	for(i=0;i<NUM_RAPL_DOMAINS;i++) {

		fd[i]=-1;

		sprintf(filename,"/sys/bus/event_source/devices/power/events/%s",
			rapl_domain_names[i]);

		fff=fopen(filename,"r");

		if (fff!=NULL) {
			fscanf(fff,"event=%x",&config);
			// printf("Found config=%d\n",config);
			fclose(fff);
		} else {
			continue;
		}

		sprintf(filename,"/sys/bus/event_source/devices/power/events/%s.scale",
			rapl_domain_names[i]);
		fff=fopen(filename,"r");

		if (fff!=NULL) {
			fscanf(fff,"%lf",&scale[i]);
			// printf("Found scale=%g\n",scale[i]);
			fclose(fff);
		}

		sprintf(filename,"/sys/bus/event_source/devices/power/events/%s.unit",
			rapl_domain_names[i]);
		fff=fopen(filename,"r");

		if (fff!=NULL) {
			fscanf(fff,"%s",units);
			// printf("Found units=%s\n",units);
			fclose(fff);
		}

		attr.type=type;
		attr.config=config;

		fd[i]=perf_event_open(&attr,-1,core,-1,0);
		if (fd[i]<0) {
			if (errno==EACCES) {
				printf("Permission denied; run as root or adjust paranoid value\n");
				return -1;
			}
			else {
				printf("error opening: %s\n",strerror(errno));
				return -1;
			}
		}
	}

	struct timespec ts1, ts2;

	clock_gettime(CLOCK_MONOTONIC_RAW, &ts1);

	// printf("\nExecuting \"%s\"\n", progname);
	usleep((int)(1000000.*t));

	clock_gettime(CLOCK_MONOTONIC_RAW, &ts2);

	struct timespec diff_time = diff(ts1,ts2);
	double n_time = diff_time.tv_sec + diff_time.tv_nsec/1e9;

	printf("domain, energy, time, avg_power\n");
	for(i=0;i<NUM_RAPL_DOMAINS;i++) {
		if (fd[i]!=-1) {
			read(fd[i],&value,8);
			close(fd[i]);

			printf("%s, %lf, %lf, %lf\n",
				rapl_domain_names[i],
				(double)value*scale[i],n_time, (double)value*scale[i]/n_time);
		}
	}

	return 0;
}

int main(int argc, char **argv) {

	int c;
	int core=0;
	int result=-1;
	int n = 0;

	opterr=0;

	while ((c = getopt (argc, argv, "c:h")) != -1) {
		switch (c) {
		case 'c':
			core = atoi(optarg);
			break;
		case 'h':
			printf("Usage: %s [-c core] [-h] TIME\n\n",argv[0]);
			exit(0);
		default:
			exit(-1);
		}
	}


	if(optind >= argc)
	{
		printf("Usage: %s [-c core] [-h] TIME\n\n",argv[0]);
		exit(0);
	}

	float t = atof(argv[optind]);

	result=rapl_perf(core, t);

	if (result<0) {

		printf("Unable to read RAPL counters.\n");
		printf("\n");

		return -1;

	}

	return 0;
}
