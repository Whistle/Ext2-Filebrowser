#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>

char *ext2_buffer;
int blocksize;
struct entry *anker=0;
struct ext2_super_block *sb=0;
char *null_block;


struct entry {
	int inode;
	int file_type;
	char *name;
	struct entry *nxt;
};

void signal_handler(int sig) {
	if(sig==SIGINT)
		exit(EXIT_FAILURE);
}

int calc_blocksize(int blockvalue) {
	switch(blockvalue) {
		case 0:
			return 1024;
			break;
		case 1:
			return 2048;
			break;
		case 2:
			return 4096;
			break;
		default:
			return 1024;
	}
}

struct ext2_super_block *superblock(char *dump, double filesize) {
	char *temp;
	int i=0;

	while(1) {
		/* Pointer um 512 weiter bewegen, falls ein MBR vorhanden sein sollte */
		temp=dump+i*512;
		i++;

		/* Ueberpruefen ob das Dateieinde schon erreicht wurde */
		if( (temp-dump) > filesize)
			return 0;

		sb=(struct ext2_super_block *)temp;
		/* Gefunden? */
		if(sb->s_magic == EXT2_SUPER_MAGIC && (sb->s_log_block_size==0 || sb->s_log_block_size==1 || sb->s_log_block_size==2)) {
			printf("\n\n======================================\n");
			printf("Superblock-Informationen:\n");
			printf("Start-Position:\t\t%d\n", (char *)sb-ext2_buffer);
			printf("Blockgroesse:\t\t%d\n", sb->s_log_block_size);
			printf("Blockgruppen-Nr:\t%d\n", sb->s_block_group_nr);
			printf("Indodes pro Gruppe:\t%d\n", sb->s_inodes_per_group);
			printf("Bloecke:\t\t%d\n",sb->s_blocks_count);
			printf("======================================\n\n");
			return sb;
		}
	}
	return 0;
}

char * get_block(char *buffer, unsigned int block, int blocksize) {
	char *temp=buffer;
	if(block>sb->s_blocks_count) {
		printf("Der Block liegt nicht mehr im FS\n");
		exit(7);
	}
	temp+=block*blocksize;
	return temp;
}

struct ext2_inode *get_inode(int inode) {
	struct ext2_group_desc *gd=0;
	struct ext2_inode *ino=0;

	gd=(struct ext2_group_desc *) get_block((char *)sb, 1, blocksize - ((char *)sb-ext2_buffer) % (int)blocksize);
	gd+=inode/(int)sb->s_inodes_per_group;
	ino=(struct ext2_inode *) get_block(ext2_buffer, gd->bg_inode_table, blocksize);
	ino+=inode%(int)sb->s_inodes_per_group-1;
	return ino;	
}

void get_file(int inode) {
	int i=0,j=0,k=0,found=0;
	int *einfach;
	int *zweifach;
	int rfilesize=0;
	struct ext2_inode *ino;
	struct entry *e;
	char *filename=0;
	char *md5cmd;
	FILE *fd;
	ino=get_inode(inode);
	for(e=anker; e; e=e->nxt) {
		if(inode==e->inode && e->file_type==1) {
			filename=e->name;
			found=1;
		}
	}
	if(!found) {
		printf("Es konnte keine regulaere Datei mit der Inode %d gefunden werden!\n",inode);
		return;
	}
	md5cmd=(char *)malloc(strlen(filename)+strlen("md5sum ")+1);
	strcpy(md5cmd,"md5sum ");
	strcat(md5cmd, filename);
	fd=fopen(filename, "w+");
	while(rfilesize<ino->i_size && i<14) {
		if(i<12) {
			rfilesize+=blocksize;
			/*printf("Block: %d\n",ino->i_block[i]);*/
			/*Enthaelt die Nummer des Blocks die die gewuenschten Daten enthaelt*/
			if(ino->i_block[i]) {
				if(rfilesize>ino->i_size) {
					fwrite(get_block(ext2_buffer,ino->i_block[i],blocksize),1,blocksize-(rfilesize-ino->i_size),fd);
					break;
				} else {
					fwrite(get_block(ext2_buffer,ino->i_block[i],blocksize),1,blocksize,fd);
				}
			} else {
				if(rfilesize>ino->i_size) {
					fwrite(null_block,1,blocksize-(rfilesize-ino->i_size),fd);
					break;
				} else {
					fwrite(null_block,1,blocksize,fd);
				}
			} 
		}
		if(i==12) {
			/*Enthaelt die Nummer des inderekten Blocks*/
			einfach=(int *)get_block(ext2_buffer, ino->i_block[i], blocksize);
			for(j=0; j<(int)blocksize/4; j++) {
				rfilesize+=blocksize;
				/*printf("Block: %d j: %d\n",einfach[j], j);*/
				if(einfach[j] && ino->i_block[12]) {
					if(rfilesize>ino->i_size) {
						fwrite(get_block(ext2_buffer,einfach[j],blocksize),1,blocksize-(rfilesize-ino->i_size),fd);
						break;
					} else {
						fwrite(get_block(ext2_buffer,einfach[j],blocksize),1,blocksize,fd);
					}
				} else {
					if(rfilesize>ino->i_size) {
						fwrite(null_block,1,blocksize-(rfilesize-ino->i_size),fd);
						break;
					} else {
						fwrite(null_block,1,blocksize,fd);
					}
				}
			}
		}
		if(i==13){
			einfach=(int *)get_block(ext2_buffer, ino->i_block[i], blocksize);
			for(j=0; j<(int)blocksize/4; j++) {
				zweifach=(int *)get_block(ext2_buffer, einfach[j],blocksize);
				/*printf("j: %d",j);*/
				for(k=0; k<(int)blocksize/4; k++) {
					rfilesize+=blocksize;
					/*printf("Block: %d k:%d\n",zweifach[k],k);*/
					if(zweifach[k] && einfach[j] && ino->i_block[i]) {
						if(rfilesize>ino->i_size) {
							fwrite(get_block(ext2_buffer,zweifach[k],blocksize),1,blocksize-(rfilesize-ino->i_size),fd);
							break;
						} else {
							fwrite(get_block(ext2_buffer,zweifach[k],blocksize),1,blocksize,fd);
						}
					} else {
						if(rfilesize>ino->i_size) {
							fwrite(null_block,1,blocksize-(rfilesize-ino->i_size),fd);
							break;
						} else {
							fwrite(null_block,1,blocksize,fd);
						}
					}

				}
				if(rfilesize>=ino->i_size)
					break;
			}
		}
		i++;	
	}
	fclose(fd);
	if(rfilesize>=ino->i_size) {
		printf("Datei wurde erfolgreich extrahiert!\n");
		printf("Info Filesize: %d\n",ino->i_size);
		system(md5cmd);
	} else {
		printf("Datei reicht bis in Dreifach-Indirekt. Bitte ausprogrammieren.\n");
	}
	return;
}


void add_entry(struct ext2_dir_entry_2 *de) {
	struct entry *e=(struct entry *)malloc(sizeof(struct entry));
	if(!e)
		exit(5);
	e->inode=de->inode;
	e->nxt=anker;
	e->file_type=de->file_type;
	anker=e;
	e->name=(char *)malloc(de->name_len+1);
	if(!e->name)
		exit(6);
	memcpy(e->name,de->name,de->name_len);
	*(e->name+de->name_len)=0;
}

void print_dentry(struct ext2_dir_entry_2 *de, unsigned char depth) {
	int len=0, count=0;
	for(count=0; count<depth; count++) {
		printf("\t");
	}
	printf("Inode: %d\tFileType: %d\tFilename: ", de->inode, de->file_type);
	for(len=0; len<de->name_len; len++) {
		printf("%c", de->name[len]);
	}
	printf("\n");
}


void print_hierachy(int inode, unsigned char depth) {
	struct ext2_dir_entry_2 *de=0;
	struct ext2_inode *ino=0;
	int rec_len_sum=0;

	ino=get_inode(inode);
	if(depth==0)
		printf("\nVerzeichnis-Struktur im Ext2-Dump\n\n");

	/* Gib . aus*/
	de=(struct ext2_dir_entry_2 *) get_block(ext2_buffer, ino[0].i_block[0], blocksize);
	rec_len_sum+=de->rec_len;
	print_dentry(de,depth);
	/* Gib .. aus*/
	de=(struct ext2_dir_entry_2 *) get_block((char *) de, 1, de->rec_len);
	print_dentry(de,depth);
	rec_len_sum+=de->rec_len;

	/*Und jetzt den Rest*/
	while(rec_len_sum<ino->i_size) {
		de=(struct ext2_dir_entry_2 *) get_block((char *) de, 1, de->rec_len);
		print_dentry(de,depth);
		add_entry(de);
		rec_len_sum+=de->rec_len;
		if(inode!=de->inode&&de->file_type==2) {
			print_hierachy(de->inode,depth+1);
		}
	}
}

/* Den simple Entry-Cache leeren */
void free_entry_list() {
	struct entry *e,*temp;	
	printf("\nLeere Cache ...\n");
	for(e=anker; e; e=temp->nxt) {
		temp=e;
		free(e->name);
		free(e);
	}
}

int main (int argc, char **argv)
{
	struct sigaction action, oldaction;
	struct ext2_super_block *sb;
	FILE * ext2dump;
	int filesize;
	size_t result;
	int command=1;
	char number_buf[20];

	action.sa_handler=signal_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags=0;

	/* Komme was wolle den Speicher wieder freigeben */
	atexit(free_entry_list);


	if(sigaction(SIGINT, &action, &oldaction) < 0) {
		printf("Konnte Handler nicht installieren: %s.\n", strerror(errno));
		return(EXIT_FAILURE);
	}


	ext2dump=fopen(argv[1], "r");
	if(ext2dump==0) { 
		fputs("Datei konnte nicht geoeffnet werden!\n",stderr); 
		exit(1);
	}

	/* Dateigroesse ermitteln */
	fseek(ext2dump,0,SEEK_END);
	filesize=ftell(ext2dump);
	rewind(ext2dump);

	/* Speicher reservieren*/
	ext2_buffer=(char *)malloc(filesize);
	if(ext2_buffer==0) {
		fputs("Speicher kann nicht reserviert werden\n",stderr);
		exit(2);
	}

	/* Datei in den reservierten Speicher lesen */
	result=fread(ext2_buffer,1,filesize,ext2dump);
	if(result!=filesize) {
		fputs("Es konnte nicht alle Daten gelesen werden\n",stderr);
		exit(3);
	}
	sb=superblock(ext2_buffer, filesize);
	if(!sb)
		exit(4);
	blocksize=calc_blocksize(sb->s_log_block_size);
	null_block=(char *)calloc(1, (int)blocksize);
	print_hierachy(EXT2_ROOT_INO,0);


	fputs("\nBitte Inode der zu extrahierenden Datei eingeben: ( oder 0 zum Beenden )",stdout);
	while(1) {
		fgets(number_buf,sizeof(number_buf), stdin);
		sscanf(number_buf,"%d",&command);
		if(!command)
			break;
		get_file(command);
	}
	/* Dieser Punkt sollte nie erreicht werden. Freigabe geschieht
	   durch den Signal Handler */
	fclose(ext2dump);
	free(ext2_buffer);
	free(null_block);
	return EXIT_SUCCESS;
}
