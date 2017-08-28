#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include"fdfs_api.h"

#include "fcgi_stdio.h"
#include "fcgi_config.h"
#include "make_log.h"
#include "util.h"
#include "fdfs_api.h"
#include "redis_op.h"
#include <time.h>



#define FCGI        "fcgi"
#define FCGI_UPLOAD "upload"
#define FILE_USER_LIST "FILE_INFO_LIST"
#define FILEID_NAME_HASH "FILEID_NAME_HASH"
#define FILEID_TIME_HASH "FILEID_TIME_HASH"
#define FILEID_USER_HASH "FILEID_USER_HASH"
#define FILEID_PV_HASH "FILEID_PV_HASH"
#define LOG_MODULE "test"
#define LOG_PROC "redis_op"

#define TIME_STR_MAX (256)


int main ()
{

    char boundary[256]= {0};//存放分割线
    char content[256] ={0}; //存放第二行文件基本信息全部数据
    char filename[256] = {0};//文件名


    while (FCGI_Accept() >= 0) {
        char *contentLength = getenv("CONTENT_LENGTH");
        int len;

        printf("Content-type: text/html\r\n"
                "\r\n");

        if (contentLength != NULL) {
            len = strtol(contentLength, NULL, 10);
        }
        else {
            len = 0;
        }

        if (len <= 0) {
            printf("No data from standard input\n");
        }
        else {
            int i, ch;
            char *begin = NULL;

            char *file_buf = NULL;
            char *p ,*q, *k= NULL;

            file_buf = malloc(len);
            memset(file_buf, 0, len);
            p = file_buf;

            for (i = 0; i < len; i++) {
                if ((ch = getchar()) < 0) {
                    printf("Error: Not enough bytes received on standard input<p>\n");
                    break;
                }
                //putchar(ch);
                *p = ch;
                p++;
            }

            //将file_buf 存到本地，进行分析
            FILE *post_fp = fopen("./post_data", "w");
            fwrite(file_buf, len , 1, post_fp);
            fclose(post_fp);

            //1 得到分割线
		begin = file_buf;
		p = strstr(begin,"\r\n");
		if(p == NULL){
			LOG(FCGI, FCGI_UPLOAD, "wrong, no boundary...");
                	goto END;
		}

		strncpy(boundary,begin,p-begin);		

            //\r\n
		p+=2;

            //得到处理完第一行剩余的长度
		len -= (p-begin);

            //此时begin指向第二行
		begin =p;

            //2 ========= 得到filename

            
		p = strstr(begin,"\r\n");
		if(p == NULL){
			LOG(FCGI, FCGI_UPLOAD, "wrong, no boundary...");
                	goto END;
		}
		         	
		strncpy(content,begin,p-begin);		
  
		q=strstr(begin,"filename=");
		q += strlen("filename=");
		q++;

		k=strchr(q,'"');
		strncpy(filename,q,k-q);
		
		printf("filename = [%s]",filename);

            //\r\n
		p+=2;
		len -=(p-begin);
		begin = p;		
		

		p = strstr(begin,"\r\n");
		if(p == NULL){
			LOG(FCGI, FCGI_UPLOAD, "wrong, no boundary...");
                	goto END;
		}
		

		p+=2;
		len -=(p-begin);
		begin = p;		


		p = strstr(begin,"\r\n");
		if(p == NULL){
			LOG(FCGI, FCGI_UPLOAD, "wrong, no boundary...");
                	goto END;
		}
		

		p+=2;
		len -=(p-begin);
		begin = p;

            //从begin开始找到第一个出现分割线等地址
		
		p = memstr(begin,len,boundary);
		if(p == NULL){
			LOG(FCGI, FCGI_UPLOAD, "wrong, no boundary...");
                	goto END;
		}
		else{
			p-=2;		
		}

            //file_len  =  p -begin
		int fd =0;
		fd= open(filename,O_CREAT|O_WRONLY,0644);
		if (fd < 0) {
             	   printf("open %s erorr", filename);
               	   goto END;
                 }
		
		ftruncate(fd,(p-begin));//扩容

		write(fd,begin,p-begin);
		
		close(fd);
		
		printf("file is upload OK!");


            //3 将filename 上传到fastDFS里 
		int ret =0;

		char fileid[1024]={0};

		ret =fdfs_upload_by_filename(filename,fileid);
		if(ret !=0)
		{
			printf("error \n");
			exit(1);
		}	
		printf("fileid = %s\n",fileid);		


            //4 将filename 的一些关系 入库redis 
		redisContext *conn = NULL; //redis连接句柄


		conn = rop_connectdb_nopwd("127.0.0.1", "6379") ;

		if (conn == NULL) {
			LOG(LOG_MODULE, LOG_PROC, "conntect error");
			goto END;
		}

		ret = rop_list_push(conn,"FILE_INFO_LIST",fileid);

		if (ret != 0) {
			LOG(LOG_MODULE, LOG_PROC, "list push error");
		        goto END;
		}	
		printf("FILE_INFO_LIST save ok\n");

		ret = rop_hset_hash(conn,FILEID_NAME_HASH,fileid,filename);
		if (ret != 0) {
			LOG(LOG_MODULE, LOG_PROC, "filename hset error");
			goto END;			
		} 
		time_t timer;
		struct tm *tblock;
		timer =time(NULL);
		tblock = localtime(&timer);


		ret = rop_hset_hash(conn,FILEID_TIME_HASH,fileid,asctime(tblock));
		if (ret != 0) {
			LOG(LOG_MODULE, LOG_PROC, "time hset error");
			goto END;
		} 
		ret = rop_hset_hash(conn,FILEID_USER_HASH,fileid,"kite9999");
		if (ret != 0) {
			LOG(LOG_MODULE, LOG_PROC, "user hset error");
			goto END;
		} 
		ret = rop_hset_hash(conn,FILEID_PV_HASH,fileid,"1");
		if (ret != 0) {
			LOG(LOG_MODULE, LOG_PROC, "download hset error");
			goto END;
		}	
	 
		printf("%s save ok\n",fileid);
		rop_disconnect(conn);


            //5 将临时文件删除

            unlink(filename);




END:
            memset(content, 0, 256);
            memset(boundary, 0, 256);
            memset(filename, 0, 256);
            if (file_buf != NULL)  {
                free(file_buf);
            }
        }
    } /* while */

    return 0;
}
