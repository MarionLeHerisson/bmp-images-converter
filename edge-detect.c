/*
	//rm a.out && gcc-9 edge-detect.c bitmap.c -O2 -ftree-vectorize -fopt-info -mavx2 -fopt-info-vec-all -pthread
	//UTILISER UNIQUEMENT DES BMP 24bits
	//./a.out "./in/" "./out/" 3 edge-detect

	// TODO : à l'initialisation des threads producteurs, pour faire simple,
              on leur passera la liste des fichiers a traiter

    // TODO : - Architecture producteur-consommateur
              - N producteurs (convertissent les fichiers puis les mettent dans la pile partagée)
              - 1 consommateur (se sert dans la pile partagée et écrit les fichiers sur le disque)

*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include "bitmap.h"
#include <stdint.h>
#include <dirent.h>
#include <stdbool.h>

#define DIM 3
#define LENGHT DIM
#define OFFSET DIM /2

const float BOXBLUR[DIM][DIM] = {{ 1, 1, 1},
                                 { 1, 1, 1},
                                 { 1, 1, 1}};

const float EDGEDETECT[DIM][DIM] = {{-1,-1,-1},
                                    {-1, 8,-1},
                                    {-1,-1,-1}};

const float SHARPEN[DIM][DIM] = {{ 0,-1, 0},
                                 {-1, 5,-1},
                                 { 0,-1, 0}};


typedef struct Color_t {
	float Red;
	float Green;
	float Blue;
} Color_e;


#define STACK_MAX 10

typedef struct stack_t {
    float kernel[DIM][DIM];
	int nb_active_threads;
	int count;
	int nb_files;
	int max_active_threads;
	int is_blur;
	char* img_names[STACK_MAX];
    Image imgs[STACK_MAX];
    Image new_imgs[STACK_MAX];
	pthread_mutex_t lock;
	pthread_cond_t can_consume;
	pthread_cond_t can_produce;
} Stack;

static Stack stack;

void stack_init() {
	pthread_cond_init(&stack.can_produce, NULL);
	pthread_cond_init(&stack.can_consume, NULL);
	pthread_mutex_init(&stack.lock, NULL);
	stack.nb_active_threads = 0;
	stack.count = 0;
}

void* producer(char* input_name);
void* producer(char* input_name) {
    printf("je suis un producer\n");

    while (true) {
        pthread_mutex_lock(&stack.lock);

        if(stack.nb_active_threads < stack.max_active_threads) {    // Si un thread est dispo

            char img_origin_name[50];
            strcpy(img_origin_name, input_name);
            strcat(img_origin_name, stack.img_names[stack.count]);

            printf("Open bmp %s\n", img_origin_name);
            stack.imgs[stack.count] = open_bitmap(img_origin_name); // fichier converti + mis dans la pile

            printf("Applying effect\n");
            apply_effect(&stack.imgs[stack.count], &stack.new_imgs[stack.count]);

            printf("%s convertie et ajoutée à la stack\n", stack.img_names[stack.count]);

            stack.count++;
            stack.nb_active_threads++;

            pthread_cond_signal(&stack.can_consume);
        }
        else {
            printf("Tous les threads sont occupés :(\n");
            while(stack.nb_active_threads >= stack.max_active_threads) {
                pthread_cond_wait(&stack.can_produce, &stack.lock);
            }
            printf("Je peux a nouveau produire :)\n");
        }
        pthread_mutex_unlock(&stack.lock);

    }

    return NULL;
}

void* consumer(char* output_name);
void* consumer(char* output_name) {     // prend un fichier de la pile et l'écrit sur disque
    printf("Je suis un consumer\n");

    int nb_files_converted = 0;

    while(true) {
        pthread_mutex_lock(&stack.lock);

            while(stack.count == 0) {
                printf("Rien a convertir :( \n");
                pthread_cond_wait(&stack.can_consume, &stack.lock);
            }

            stack.count--;

            char img_destination_name[50];
            strcpy(img_destination_name, output_name);
            strcat(img_destination_name, stack.img_names[stack.count]);

            printf("Save bmp %s\n", img_destination_name);
            save_bitmap(stack.new_imgs[stack.count], img_destination_name);

            nb_files_converted++;
            printf("nb_files_converted = %d\n", nb_files_converted);

            if(nb_files_converted >= stack.nb_files) {
                printf("All %d files saved !\n", stack.nb_files);

                //on ne débloque pas le mutex, pour qu'ils arretent de produire
                break;
            }
            pthread_cond_signal(&stack.can_produce);
        pthread_mutex_unlock(&stack.lock);
    }

    return NULL;
}

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

int verify_input_folder(char* input_name, int nb_thread);
int verify_input_folder(char* input_name, int nb_thread) {

    DIR* inp = opendir(input_name);
    struct dirent* entry;

    if (inp == NULL) {
        perror(input_name);
        return -1;
    }

    while((entry = readdir(inp))) {
        if (strstr(entry->d_name, ".bmp\0") != NULL) {
            stack.img_names[stack.nb_files] = entry->d_name;
            stack.nb_files ++;
        }
    }

    closedir(inp);
//    rewinddir(inp);

    if (stack.nb_files == 2) {    // . and ..
        printf ("Empty folder.\n");
        return 0;
    } else if (nb_thread > stack.nb_files || nb_thread < 0) {
        printf("Number of threads must be between 0 and number of files to convert\n");
        return -1;
    }

    printf("Counted %d files in the input folder.\n", stack.nb_files);
    return 0;
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

int verify_effect(char* effect);
int verify_effect(char* effect) {

    if(strcmp(effect, "boxblur\0") == 0) {
        memcpy(stack.kernel, BOXBLUR, sizeof (float) * DIM * DIM);
        stack.is_blur = 1;
    }
    else if(strcmp(effect, "edge-detect\0") == 0) {
        memcpy(stack.kernel, EDGEDETECT, sizeof (float) * DIM * DIM);
        stack.is_blur = 0;
    }
    else if(strcmp(effect, "sharpen\0") == 0) {
        memcpy(stack.kernel, SHARPEN, sizeof (float) * DIM * DIM);
        stack.is_blur = 0;
    }
    else {
        printf("Error : unknown effect %s\n", effect);
        return -1;
    }

    return 0;
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

                    if (stack.is_blur) {
                    	c.Red += ((float) p->r) * stack.kernel[a][b] / 9;
                        c.Green += ((float) p->g) * stack.kernel[a][b] / 9;
                        c.Blue += ((float) p->b) * stack.kernel[a][b] / 9;
                    }
                    else {
                        c.Red += ((float) p->r) * stack.kernel[a][b];
                        c.Green += ((float) p->g) * stack.kernel[a][b];
                        c.Blue += ((float) p->b) * stack.kernel[a][b];
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

    if(verify_input_folder(input_name, nb_thread) +
        verify_effect(effect) != 0) {
        return -1;
    }
    verify_output_folder(output_name);

    stack.max_active_threads = nb_thread;

    pthread_t threads_id[nb_thread];
    stack_init();
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	for(int i = 0; i < nb_thread - 1; i++) {
		pthread_create(&threads_id[i], &attr, producer, input_name);
	}
	pthread_create(&threads_id[nb_thread], NULL, consumer, output_name);
    pthread_join(threads_id[nb_thread], NULL);

    printf("Done !\n");
	return 0;
}
