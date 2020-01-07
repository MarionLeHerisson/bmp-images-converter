/*
	//rm a.out && gcc-9 edge-detect.c bitmap.c -O2 -ftree-vectorize -fopt-info -mavx2 -fopt-info-vec-all
	//UTILISER UNIQUEMENT DES BMP 24bits
	//./a.out "./in/" "./out/" 3 edge-detect

	// TODO : à l'initialisation des threads producteurs, pour faire simple,
              on leur passera la liste des fichiers a traiter

    // TODO : - Architecture producteur-consommateur
              - N producteurs (convertissent les fichiers puis les mettent dans la pile partagée)
              - 1 consommateur (se sert dans la pile partagée et écrit les fichiers sur le disque)
              -> pile partagée = ressource bloquée
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bitmap.h"
#include <stdint.h>
#include <dirent.h>

#define DIM 3
#define LENGHT DIM
#define OFFSET DIM /2

const float boxblur = {{ 1, 1, 1},
                       { 1, 1, 1},
                       { 1, 1, 1}};
int is_blur = 0;

//const float gaussblur = {{ (1 / 16) * 1, (1 / 16) * 2, (1 / 16) * 1},
//					     { (1 / 16) * 2, (1 / 16) * 4, (1 / 16) * 2},
//				         { (1 / 16) * 1, (1 / 16) * 2, (1 / 16) * 1}};

const float edgedetect = {{-1,-1,-1},
					      {-1, 8,-1},
					      {-1,-1,-1}};

const float sharpen = {{ 0,-1, 0},
					   {-1, 5,-1},
					   { 0,-1, 0}};

const float KERNEL[DIM][DIM] = {{-1,-1,-1},
                              {-1, 8,-1},
                              {-1,-1,-1}};

typedef struct Color_t {
	float Red;
	float Green;
	float Blue;
} Color_e;

void delete_file(char* foldername, char* filename);
void delete_file(char* foldername, char* filename) {

    char img_destination_name[50];
    strcpy(img_destination_name, foldername);
    strcat(img_destination_name, filename);

    int retch = chmod(img_destination_name, 0777);
    int ret = remove(img_destination_name);

    if(retch != 0) {
       printf("Error: unable to change the file's permissions : %s\n", img_destination_name);
    }

    if(ret != 0) {
        printf("Error: unable to delete the file : %s\n", img_destination_name);
    }
}

void verify_input_folder(char* input_name, int nb_thread, DIR* inp, struct dirent* entry);
void verify_input_folder(char* input_name, int nb_thread, DIR* inp, struct dirent* entry) {

    if (inp == NULL) {
        perror(input_name);
        return -1;
    }

    int nb_files = 0;
    while((entry = readdir(inp))) {
        if (strstr(entry->d_name, ".bmp\0") != NULL) {
            nb_files ++;
        }
    }
    rewinddir(inp);

    if (nb_files == 2) {    // . and ..
        printf ("Empty folder.\n");
        return 0;
    } else if (nb_thread > nb_files || nb_thread < 0) {
        printf("Number of threads must be between 0 and number of files to convert\n");
        return -1;
    }
}

void verify_output_folder(char* output_name);
void verify_output_folder(char* output_name) {

    struct dirent* entry;
    DIR* outp = opendir(output_name);

    if (outp == NULL) {
        perror(output_name);
        mkdir(output_name, 0777);
    }
    else {
        while((entry = readdir(outp))) {
            if (strstr(entry->d_name, ".bmp\0") != NULL) {
                delete_file(output_name, entry->d_name);
            }
        }

        closedir(outp);
    }
}

void apply_effect(Image* original, Image* new_i);
void apply_effect(Image* original, Image* new_i) {

	int w = original->bmp_header.width;
	int h = original->bmp_header.height;

	*new_i = new_image(w, h, original->bmp_header.bit_per_pixel, original->bmp_header.color_planes);

	for (int y = OFFSET; y < h - OFFSET; y++) {
		for (int x = OFFSET; x < w - OFFSET; x++) {
			Color_e c = { .Red = 0, .Green = 0, .Blue = 0};

			for(int a = 0; a < LENGHT; a++){
				for(int b = 0; b < LENGHT; b++){
					int xn = x + a - OFFSET;
					int yn = y + b - OFFSET;

					Pixel* p = &original->pixel_data[yn][xn];

                    if (is_blur) {
                    	c.Red += ((float) p->r) * KERNEL[a][b] / 9;
                        c.Green += ((float) p->g) * KERNEL[a][b] / 9;
                        c.Blue += ((float) p->b) * KERNEL[a][b] / 9;
                    }
                    else {
                        c.Red += ((float) p->r) * KERNEL[a][b];
                        c.Green += ((float) p->g) * KERNEL[a][b];
                        c.Blue += ((float) p->b) * KERNEL[a][b];
                    }
				}
			}

			Pixel* dest = &new_i->pixel_data[y][x];
			dest->r = (uint8_t)  (c.Red <= 0 ? 0 : c.Red >= 255 ? 255 : c.Red);
			dest->g = (uint8_t) (c.Green <= 0 ? 0 : c.Green >= 255 ? 255 : c.Green);
			dest->b = (uint8_t) (c.Blue <= 0 ? 0 : c.Blue >= 255 ? 255 : c.Blue);
		}
	}
}

int main(int argc, char** argv) {

    if (argv[4] == NULL || argv[3] == NULL || argv[2] == NULL || argv[1] == NULL) {
        printf("Invalid number of arguments. The command must look like :\n./apply-effect \"./in/\" \"./out/\" 3 boxblur\n");
        return -1;
    }

    char* input_name = argv[1];
    char* output_name = argv[2];
    int nb_thread = atoi(argv[3]);
    char* effect = argv[4];

    struct dirent* entry;
    DIR* inp = opendir(input_name);

    verify_input_folder(input_name, nb_thread, inp, entry);
    verify_output_folder(output_name);

    Image img;
    Image new_i;

    while ((entry = readdir(inp))) {
        printf("Processing '%s'...\n", entry->d_name);

        if (strstr(entry->d_name, ".bmp\0") != NULL) {

            // copying names
            char img_origin_name[50];
            strcpy(img_origin_name, input_name);
            strcat(img_origin_name, entry->d_name);

            char img_destination_name[50];
            strcpy(img_destination_name, output_name);
            strcat(img_destination_name, entry->d_name);

            printf("Open bmp %s\n", img_origin_name);
            img = open_bitmap(img_origin_name);

            printf("Applying effect\n");
            apply_effect(&img, &new_i);

            printf("Save bmp %s\n", img_destination_name);
            save_bitmap(new_i, img_destination_name);
        }

        printf("Next\n\n");
    }

	closedir(inp);

    printf("Done !\n");
	return 0;
}
