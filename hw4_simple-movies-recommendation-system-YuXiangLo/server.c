#include "header.h"

movie_profile* movies[MAX_MOVIES];
char* Movies[32][MAX_MOVIES];
double Pts[32][MAX_MOVIES];
int sz[32];
char** Mmap;
double* Pmap;

unsigned int num_of_movies = 0;
unsigned int num_of_reqs = 0;

// Global request queue and pointer to front of queue
// TODO: critical section to protect the global resources
request* reqs[MAX_REQ];
int front = -1;

/* Note that the maximum number of processes per workstation user is 512. * 
 * We recommend that using about <256 threads is enough in this homework. */
pthread_t tid[MAX_CPU][MAX_THREAD]; //tids for multithread

//#ifdef PROCESS
pid_t pid[MAX_CPU][MAX_THREAD]; //pids for multiprocess
//#endif

//mutex
pthread_mutex_t lock; 

void initialize(FILE* fp);
void Filter(movie_profile** movies, char* s, int id);
void* Tmerge(void *data);
void Pmerge(int l, int r, int id);
request* read_request();
int pop();

int pop(){
	front+=1;
	return front;
}

pthread_t t;

int main(int argc, char *argv[]){

	if(argc != 1){
#ifdef PROCESS
		fprintf(stderr,"usage: ./pserver\n");
#elif defined THREAD
		fprintf(stderr,"usage: ./tserver\n");
#endif
		exit(-1);
	}
	FILE *fp;

	if ((fp = fopen("./data/movies.txt","r")) == NULL){
		ERR_EXIT("fopen");
	}

	initialize(fp);
#ifdef THREAD
	for(int i = 0; i < num_of_reqs; i++){
		Tseg* new = malloc(sizeof(Tseg));
		Filter(movies, reqs[i]->keywords, i);
		new->id = i; new->st = 0; new->end = sz[i]; new->num = 1;
		pthread_create(&tid[i][1], NULL, Tmerge, new);
	}
	FILE *logfp;
	for(int i = 0; i < num_of_reqs; i++){
		pthread_join(tid[i][1], NULL);
		char buf[20]; sprintf(buf, "%dt.out", reqs[i]->id);
		logfp = fopen(buf, "w");
		for(int j = 0; j < sz[i]; j++)
			fprintf(logfp, "%s\n", Movies[i][j]);
		fclose(logfp);
	}
#elif defined PROCESS
	Mmap = mmap(NULL, MAX_MOVIES * sizeof(char*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	Pmap = mmap(NULL, MAX_MOVIES * sizeof(double), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	Filter(movies, reqs[0]->keywords, 0);
	Pmerge(0, sz[0], 1);
		char buf[20]; sprintf(buf, "%dp.out", reqs[0]->id);
		FILE *logfp;
		logfp = fopen(buf, "w");
		for(int j = 0; j < sz[0]; j++)
			fprintf(logfp, "%s\n", Mmap[j]);
		fclose(logfp);
#endif
	assert(fp != NULL);
	fclose(fp);	

	return 0;
}

void Pmerge(int l, int r, int id){
	if(id >= 8 || r - l <= 1000){
		sort(Movies[0] + l, Pts[0] + l, r - l);
		for(int i = l; i < r; i++){
			strcpy(Mmap[i], Movies[0][i]);
			Pmap[i] = Pts[0][i];
		}
		return;
	}
	if((pid[0][id * 2] = fork()) == 0){
		Pmerge(l, (l + r) / 2, id * 2);	
	} else if((pid[0][id * 2 + 1] = fork()) == 0){
		Pmerge((l + r) / 2, r, id * 2 + 1);
	} else{
		wait(NULL);
		wait(NULL);
		char** tmpMovies = malloc((r - l) * sizeof(char*));
		double* tmpPts = malloc((r - l) * sizeof(double));
		int sz, rss, lss;
		for(int i = 0, ls = l, rs = (l + r)  /  2; i < r - l; i++){
			if(Pmap[ls] < Pmap[rs] ||\
					Pmap[ls] == Pmap[rs] && strcmp(Mmap[ls], Mmap[rs]) > 0){
				tmpPts[i] = Pmap[rs];
				tmpMovies[i] = Mmap[rs++];
			} else{
				tmpPts[i] = Pmap[ls];
				tmpMovies[i] = Mmap[ls++];
			}
			if(rs == r || ls == (l + r) / 2){
				sz = i + 1;
				lss = ls;
				rss = rs;
				break;
			}
		}
		while(rss != r){
			tmpPts[sz] = Pmap[rss];
			tmpMovies[sz++] = Mmap[rss++];
		}
		while(lss != (l + r) / 2){
			tmpPts[sz] = Pmap[lss];
			tmpMovies[sz++] = Mmap[lss++];
		}
		for(int i = l, j = 0; i < r; i++, j++){
			Mmap[i] = tmpMovies[j];
			Pmap[i] = tmpPts[j];
		}
		free(tmpMovies);
		free(tmpPts);
	}
}

void* Tmerge(void* d){
	Tseg *data = (Tseg*)d;
	if(data->num >= 8 || data->end - data->st <= 1000){
		sort(Movies[data->id] + data->st, Pts[data->id] + data->st, data->end - data->st);
		return NULL;
	}
	Tseg *left = malloc(sizeof(Tseg));
	Tseg *right = malloc(sizeof(Tseg));
	left->id = right->id = data->id;
	left->st = data->st; right->end = data->end;
	left->end = right->st = (data->st + data->end) / 2;
	left->num = data->num * 2; right->num = left->num + 1;
	pthread_create(&tid[left->id][left->num], NULL, Tmerge, left);
	pthread_create(&tid[right->id][right->num], NULL, Tmerge, right);
	pthread_join(tid[left->id][left->num], NULL);
	pthread_join(tid[right->id][right->num], NULL);	
	char** tmpMovies = malloc((data->end - data->st) * sizeof(char*));
	double* tmpPts = malloc((data->end - data->st) * sizeof(double));
	int sz, rss, lss;
	for(int i = 0, ls = left->st, rs = right->st; i < data->end - data->st; i++){
		if(Pts[data->id][ls] < Pts[data->id][rs] ||\
				Pts[data->id][ls] == Pts[data->id][rs] && strcmp(Movies[data->id][ls], Movies[data->id][rs]) > 0){
			tmpPts[i] = Pts[data->id][rs];
			tmpMovies[i] = Movies[data->id][rs++];
		} else{
			tmpPts[i] = Pts[data->id][ls];
			tmpMovies[i] = Movies[data->id][ls++];
		}
		if(rs == right->end || ls == left->end){
			sz = i + 1;
			lss = ls;
			rss = rs;
			break;
		}
	}
	while(rss != right->end){
		tmpPts[sz] = Pts[data->id][rss];
		tmpMovies[sz++] = Movies[data->id][rss++];
	}
	while(lss != left->end){
		tmpPts[sz] = Pts[data->id][lss];
		tmpMovies[sz++] = Movies[data->id][lss++];
	}
	for(int i = data->st, j = 0; i < data->end; i++, j++){
		Movies[data->id][i] = tmpMovies[j];
		Pts[data->id][i] = tmpPts[j];
	}
	free(tmpMovies);
	free(tmpPts);
	return NULL;
}

void Filter(movie_profile** movies, char* s, int id){
	for(int i = 0; i < num_of_movies; i++){
		if(s[0] == '*' || strstr(movies[i]->title, s)){
			Movies[id][sz[id]] = movies[i]->title;
			for(int j = 0; j < NUM_OF_GENRE; j++)
				Pts[id][sz[id]] += movies[i]->profile[j] * reqs[id]->profile[j];
			sz[id]++;
		}
#ifdef PROCESS
		Mmap[i] = mmap(NULL, MAX_LEN * sizeof(char), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
#endif
	}
}

/**=======================================
 * You don't need to modify following code *
 * But feel free if needed.                *
 =========================================**/

request* read_request(){
	int id;
	char buf1[MAX_LEN],buf2[MAX_LEN];
	char delim[2] = ",";

	char *keywords;
	char *token, *ref_pts;
	char *ptr;
	double ret,sum;

	scanf("%u %254s %254s",&id,buf1,buf2);
	keywords = malloc(sizeof(char)*strlen(buf1)+1);
	if(keywords == NULL){
		ERR_EXIT("malloc");
	}

	memcpy(keywords, buf1, strlen(buf1));
	keywords[strlen(buf1)] = '\0';

	double* profile = malloc(sizeof(double)*NUM_OF_GENRE);
	if(profile == NULL){
		ERR_EXIT("malloc");
	}
	sum = 0;
	ref_pts = strtok(buf2,delim);
	for(int i = 0;i < NUM_OF_GENRE;i++){
		ret = strtod(ref_pts, &ptr);
		profile[i] = ret;
		sum += ret*ret;
		ref_pts = strtok(NULL,delim);
	}

	// normalize
	sum = sqrt(sum);
	for(int i = 0;i < NUM_OF_GENRE; i++){
		if(sum == 0)
				profile[i] = 0;
		else
				profile[i] /= sum;
	}

	request* r = malloc(sizeof(request));
	if(r == NULL){
		ERR_EXIT("malloc");
	}

	r->id = id;
	r->keywords = keywords;
	r->profile = profile;

	return r;
}

/*=================initialize the dataset=================*/
void initialize(FILE* fp){

	char chunk[MAX_LEN] = {0};
	char *token,*ptr;
	double ret,sum;
	int cnt = 0;

	assert(fp != NULL);

	// first row
	if(fgets(chunk,sizeof(chunk),fp) == NULL){
		ERR_EXIT("fgets");
	}

	memset(movies,0,sizeof(movie_profile*)*MAX_MOVIES);	

	while(fgets(chunk,sizeof(chunk),fp) != NULL){
		
		assert(cnt < MAX_MOVIES);
		chunk[MAX_LEN-1] = '\0';

		const char delim1[2] = " "; 
		const char delim2[2] = "{";
		const char delim3[2] = ",";
		unsigned int movieId;
		movieId = atoi(strtok(chunk,delim1));

		// title
		token = strtok(NULL,delim2);
		char* title = malloc(sizeof(char)*strlen(token)+1);
		if(title == NULL){
			ERR_EXIT("malloc");
		}
		
		// title.strip()
		memcpy(title, token, strlen(token)-1);
	 	title[strlen(token)-1] = '\0';

		// genres
		double* profile = malloc(sizeof(double)*NUM_OF_GENRE);
		if(profile == NULL){
			ERR_EXIT("malloc");
		}

		sum = 0;
		token = strtok(NULL,delim3);
		for(int i = 0; i < NUM_OF_GENRE; i++){
			ret = strtod(token, &ptr);
			profile[i] = ret;
			sum += ret*ret;
			token = strtok(NULL,delim3);
		}

		// normalize
		sum = sqrt(sum);
		for(int i = 0; i < NUM_OF_GENRE; i++){
			if(sum == 0)
				profile[i] = 0;
			else
				profile[i] /= sum;
		}

		movie_profile* m = malloc(sizeof(movie_profile));
		if(m == NULL){
			ERR_EXIT("malloc");
		}

		m->movieId = movieId;
		m->title = title;
		m->profile = profile;

		movies[cnt++] = m;
	}
	num_of_movies = cnt;

	// request
	scanf("%d",&num_of_reqs);
	assert(num_of_reqs <= MAX_REQ);
	for(int i = 0; i < num_of_reqs; i++){
		reqs[i] = read_request();
	}
}
/*========================================================*/
