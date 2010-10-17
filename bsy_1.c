#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>


char *ext2_buffer;
int blocksize;
struct entry *anker=0;
struct ext2_super_block *sb=0;

struct entry {
	int inode;
	int file_type;
	char *name;
	struct entry *nxt;
};

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
			/*Steht der Superblock am Anfang der ermittelten Blockgroesse ?*/
			if((temp-dump) % (int)pow(2.0,10+sb->s_log_block_size)==0)
				return sb;
		}
	}
	return 0;
}

char * get_block(char *buffer, int block, int blocksize) {
	char *temp=buffer;
	temp+=block*blocksize;
	return temp;
}

struct ext2_inode *get_inode(int inode) {
	struct ext2_group_desc *gd=0;
	struct ext2_inode *ino=0;

	gd=(struct ext2_group_desc *) get_block((char *)sb, 1, blocksize);
	gd+=inode/(int)sb->s_inodes_per_group;
	ino=(struct ext2_inode *) get_block(ext2_buffer, gd->bg_inode_table, blocksize);
	ino+=inode%(int)sb->s_inodes_per_group-1;
	return ino;	
}

void get_file(int inode) {
	int i=0,j=0,found=0;
	int *blocks;
	__le32 rfilesize=0;
	struct ext2_inode *ino;
	struct entry *e;
	char *filename=0;
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
	fd=fopen(filename, "w+");
	while(i < 13) {
		if(i<12) {
			/*Enthaelt die Nummer des Blocks die die gewuenschten Daten enthaelt*/
			if(ino->i_block[i]) {
				rfilesize+=blocksize;
				if(rfilesize>ino->i_size) {
					fwrite(get_block(ext2_buffer,ino->i_block[i],blocksize),1,blocksize-(rfilesize-ino->i_size),fd);
					break;
				} else {
					fwrite(get_block(ext2_buffer,ino->i_block[i],blocksize),1,blocksize,fd);
				}
			}
		}
		if(i==12) {
			/*Enthaelt die Nummer des inderekten Blocks*/
			blocks=(int *)get_block(ext2_buffer, ino->i_block[12], blocksize);
			for(j=0; j<(int)(blocksize/sizeof(int)); j++)
				if(blocks[j]) {
					rfilesize+=blocksize;
					if(rfilesize>ino->i_size) {
						fwrite(get_block(ext2_buffer,blocks[j],blocksize),1,blocksize-(rfilesize-ino->i_size),fd);
						break;
					} else {
						fwrite(get_block(ext2_buffer,blocks[j],blocksize),1,blocksize,fd);
					}
				}
		}
		i++;	
	}
	fclose(fd);
	printf("Datei wurde erfolgreich extrahiert!\n");
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

<<<<<<< HEAD

void print_hierachy(int inode) {
=======
void print_hierachy(struct ext2_super_block *sb, int inode, unsigned char depth) {
>>>>>>> Level2
	struct ext2_dir_entry_2 *de=0;
	struct ext2_inode *ino=0;
	int rec_len_sum=0;

	ino=get_inode(inode);

	/* Gib . aus*/
	de=(struct ext2_dir_entry_2 *) get_block(ext2_buffer, ino[0].i_block[0], blocksize);
	rec_len_sum+=de->rec_len;
	print_dentry(de,depth);
	/* Gib .. aus*/
	de=(struct ext2_dir_entry_2 *) get_block((char *) de, 1, de->rec_len);
	print_dentry(de,depth);
	rec_len_sum+=de->rec_len;

	/*Und jetzt den Rest*/
	while(rec_len_sum<blocksize) {
		de=(struct ext2_dir_entry_2 *) get_block((char *) de, 1, de->rec_len);
		print_dentry(de,depth);
		add_entry(de);
		rec_len_sum+=de->rec_len;
		if(inode!=de->inode&&de->file_type==2) {
			print_hierachy(sb,de->inode,depth+1);
		}
	}
}


void free_entry_list() {
	struct entry *e,*temp;	
	for(e=anker; e; e=temp->nxt) {
		temp=e;
		free(e->name);
		free(e);
	}
}

int main (int argc, char **argv)
{
	struct ext2_super_block *sb;
	FILE * ext2dump;
	unsigned long filesize;
	size_t result;
	int command=1;
	char number_buf[20];
	/* Komme was wolle den Speicher wieder freigeben */
	atexit(free_entry_list);
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
	blocksize=pow(2.0, 10.0+sb->s_log_block_size);
	print_hierachy(EXT2_ROOT_INO);


	fputs("\nBitte Inode der zu extrahierenden Datei eingeben: ( oder 0 zum Beenden )",stdout);
	while(command) {
		fgets(number_buf,sizeof(number_buf), stdin);
		sscanf(number_buf,"%d",&command);
		if(!command)
			break;
		get_file(command);
	}
	
	fclose(ext2dump);
	free(ext2_buffer);
	return EXIT_SUCCESS;
}
