#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>

#define EXT2_MAGIC_NUMBER 0xEF53

struct ext2_super_block *superblock(char *dump, double filesize) {
	struct ext2_super_block *sb=0;
	char *temp;
	int i=0;

	while(1) {
		/* Pointer um 512 weiter bewegen, falls ein MBR vorhanden sein sollte */
		temp=dump+i*512;
		i++;

		/* Ueberpruefen ob das Dateieinde schon erreicht wurde */
		if( (temp-dump) > filesize)
			return 0;

		sb=(struct ext2_super_block *) temp;
		/* Gefunden? */
		if(sb->s_magic == EXT2_MAGIC_NUMBER && (sb->s_log_block_size==0 || sb->s_log_block_size==1 || sb->s_log_block_size==2)) {
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

void print_dentry(struct ext2_dir_entry_2 *de) {
	int len=0;
	printf("Inode: %d\tRec-Len: %d\tFileType: %d\tFilename: ", de->inode, de->rec_len, de->file_type);
	for(len=0; len<de->name_len; len++) {
		printf("%c", de->name[len]);
	}
	printf("\n");
}



int get_file(struct ext2_inode *ino, char *buffer, int blocksize, const char * filename) {
	int i=0,j=0;
	int *blocks;
	__le32 rfilesize=0;
	FILE *fd=fopen(filename, "w+");
	while(i < 13) {
		if(i<12) {
			/*Enthaelt die Nummer des Blocks die die gewuenschten Daten enthaelt*/
			if(ino->i_block[i]) {
				rfilesize+=blocksize;
				if(rfilesize>ino->i_size) {
					fwrite(get_block(buffer,ino->i_block[i],blocksize),1,blocksize-(rfilesize-ino->i_size),fd);
					break;
				} else {
					fwrite(get_block(buffer,ino->i_block[i],blocksize),1,blocksize,fd);
				}
			}
		}
		if(i==12) {
			/*Enthaelt die Nummer des inderekten Blocks*/
			blocks=(int *)get_block(buffer, ino->i_block[12], blocksize);
			for(j=0; j<(int)(blocksize/sizeof(int)); j++)
				if(blocks[j]) {
					rfilesize+=blocksize;
					if(rfilesize>ino->i_size) {
						fwrite(get_block(buffer,blocks[j],blocksize),1,blocksize-(rfilesize-ino->i_size),fd);
						break;
					} else {
						fwrite(get_block(buffer,blocks[j],blocksize),1,blocksize,fd);
					}
				}
		}
		i++;	
	}
	fclose(fd);
	return 0;
}



int main (int argc, char **argv)
{
	struct ext2_dir_entry_2 *de=0;
	struct ext2_super_block *sb=0;
	struct ext2_group_desc *gd=0;
	struct ext2_inode *ino=0;
	char * ext2buffer;
	FILE * ext2dump;

	unsigned long filesize;
	size_t result;
	double blocksize=0;

	ext2dump=fopen("ext2fs2.raw", "r");
	if(ext2dump==0) { 
		fputs("Datei konnte nicht geoeffnet werden!",stderr); 
		exit(1);
	}

	/* Dateigroesse ermitteln */
	fseek(ext2dump,0,SEEK_END);
	filesize=ftell(ext2dump);
	rewind(ext2dump);

	/* Speicher reservieren*/
	ext2buffer=(char *)malloc(filesize);
	if(ext2buffer==0) {
		fputs("Speicher kann nicht reserviert werden",stderr);
		exit(2);
	}

	/* Datei in den reservierten Speicher lesen */
	result=fread(ext2buffer,1,filesize,ext2dump);
	if(result!=filesize) {
		fputs("Es konnte nicht alle Daten gelesen werden",stderr);
		exit(3);
	}
	sb=superblock(ext2buffer, filesize);
	blocksize=pow(2.0, 10.0+sb->s_log_block_size);
	printf("Blocksize: %f\n", blocksize);
	gd=(struct ext2_group_desc *) get_block((char *)sb, 1, blocksize);
	printf("Inode-Table %d\n", gd->bg_inode_table);
	ino=(struct ext2_inode *) get_block(ext2buffer, gd->bg_inode_table, blocksize);

	de=(struct ext2_dir_entry_2 *) get_block(ext2buffer, ino[1].i_block[0], blocksize);
	print_dentry(de);
	de=(struct ext2_dir_entry_2 *) get_block((char *) de, 1, de->rec_len);
	print_dentry(de);
	de=(struct ext2_dir_entry_2 *) get_block((char *) de, 1, de->rec_len);
	print_dentry(de);

	fclose(ext2dump);
	free(ext2buffer);
	return 0;
}

