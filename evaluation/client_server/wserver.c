// A simple https server orginally provided by Ilias

#include "common.h"
#define KEYFILE "server.pem"
#define PASSWORD "password"
#define DHFILE "dh1024.pem"
#include <openssl/e_os2.h>


int tcp_listen()
  {
    int sock;
    struct sockaddr_in sin;
    int val=1;


    if((sock=socket(AF_INET,SOCK_STREAM,0))<0)
      err_exit("Couldn't make socket");
    
    memset(&sin,0,sizeof(sin));
    sin.sin_addr.s_addr=INADDR_ANY;
    sin.sin_family=AF_INET;
    sin.sin_port=htons(PORT);
    setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,
      &val,sizeof(val));
    
    if(bind(sock,(struct sockaddr *)&sin,
      sizeof(sin))<0)
      berr_exit("Couldn't bind");
    listen(sock,5);  

    return(sock);
  }

void load_dh_params(ctx,file)
  SSL_CTX *ctx;
  char *file;
  {
    DH *ret=0;
    BIO *bio;

    if ((bio=BIO_new_file(file,"r")) == NULL)
      berr_exit("Couldn't open DH file");

    ret=PEM_read_bio_DHparams(bio,NULL,NULL,
      NULL);
    BIO_free(bio);
    if(SSL_CTX_set_tmp_dh(ctx,ret)<0)
      berr_exit("Couldn't set DH parameters");
  }

void generate_eph_rsa_key(ctx)
  SSL_CTX *ctx;
  {
    RSA *rsa;

    rsa=RSA_generate_key(512,RSA_F4,NULL,NULL);
    
    if (!SSL_CTX_set_tmp_rsa(ctx,rsa))
      berr_exit("Couldn't set RSA key");

    RSA_free(rsa);
  }
    
  
// Just added support for files 
static int http_serve(ssl,s)
  SSL *ssl;
  int s;
  {
    char buf[BUFSIZZ];
    int r; //len; //len seems useless...
    BIO *io,*ssl_bio;
    
    io=BIO_new(BIO_f_buffer());
    ssl_bio=BIO_new(BIO_f_ssl());
    BIO_set_ssl(ssl_bio,ssl,BIO_CLOSE);
    BIO_push(io,ssl_bio);
    
    while(1){
      r=BIO_gets(io,buf,BUFSIZZ-1);

      switch(SSL_get_error(ssl,r)){
        case SSL_ERROR_NONE:
          //len=r; // useless? 
          break;
        default:
          berr_exit("SSL read problem");
      }

      /* Look for the blank line that signals
         the end of the HTTP headers */
      if(!strcmp(buf,"\r\n") ||
        !strcmp(buf,"\n"))
        break;
    }

    if((r=BIO_puts
      (io,"HTTP/1.0 200 OK\r\n"))<=0)
      err_exit("Write error");
    if((r=BIO_puts
      (io,"Server: Svarvel\r\n\r\n"))<=0)
      err_exit("Write error");
    if((r=BIO_puts
      (io,"Server test page\r\n"))<=0)
      err_exit("Write error");
  	/* Attempt to send file index.html*/
	BIO *file;
	static int bufsize = BUFSIZZ;
	int total_bytes = 0; 
	int j = 0; 
	/* Commenting since already allocated above */
	/*
	char *buf = NULL;
	static int bufsize = BUFSIZZ;
	
	// allocate buffer to send file (check if not already allocated) 
	buf=OPENSSL_malloc(bufsize);
	if (buf == NULL) 
		return(0);
	*/

	// Code below needs to be checked first 
	/*char *p;
	p="index.html"; // mmmmm...*/
	if ((file=BIO_new_file("index.html","r")) == NULL)
	{                
		BIO_puts(io, "Error opening file"); // what is text? ERROR
        //BIO_printf(io,"Error opening '%s'\r\n",p);
        BIO_printf(io,"Error opening index.html\r\n");
		goto write_error;
	}
	/*-- START -- */
            for (;;)
                {
                int i = BIO_read(file,buf,bufsize);
                if (i <= 0)
					break;
                total_bytes += i;
                fprintf(stderr,"%d\n",i);
                if (total_bytes > 3*1024)
                    {
                    total_bytes = 0;
                    fprintf(stderr,"RENEGOTIATE\n");
                    SSL_renegotiate(ssl);	// check if this works...(con --> ssl)
                    }
                for (j = 0; j < i; )
                    {
					/* Black magic START */
					static int count = 0; 
					if (++count == 13) { 
						SSL_renegotiate(ssl); 
					} 
					/* Black magic END */
                    int k = BIO_write(io, &(buf[j]), i-j);
                    if (k <= 0)
                        {
                        if (! BIO_should_retry(io))
                            goto write_error;
                        else
                            {
                            BIO_printf(io, "rewrite W BLOCK\n");
                            }
                        }
                    else
                        {
                        j+=k;
                        }
                    }

	write_error:
	            BIO_free(file);
    	        break;
                }
	

	/*--END--*/ 
    if((r=BIO_flush(io))<0)
      err_exit("Error flushing BIO");


    
    r=SSL_shutdown(ssl);
    if(!r){
      /* If we called SSL_shutdown() first then
         we always get return value of '0'. In
         this case, try again, but first send a
         TCP FIN to trigger the other side's
         close_notify*/
      shutdown(s,1);
      r=SSL_shutdown(ssl);
    }
      
    switch(r){  
      case 1:
        break; /* Success */
      case 0:
      case -1:
      default:
        berr_exit("Shutdown failed");
    }

    SSL_free(ssl);
    close(s);

    return(0);
  }
 
int main(argc,argv)
  int argc;
  char **argv;
  {
    int sock,s;
    BIO *sbio;
    SSL_CTX *ctx;
    SSL *ssl;
    int r;
    pid_t pid;
	
    /* Build our SSL context*/
    ctx=initialize_ctx(KEYFILE,PASSWORD);
    load_dh_params(ctx,DHFILE);
   
    sock=tcp_listen();

    while(1){
      if((s=accept(sock,0,0))<0)
        err_exit("Problem accepting");

      if((pid=fork())){
        close(s);
      }
      else {
        sbio=BIO_new_socket(s,BIO_NOCLOSE);
        ssl=SSL_new(ctx);
	    SSL_set_bio(ssl,sbio,sbio);
 
        if((r=SSL_accept(ssl)<=0))
          berr_exit("SSL accept error");
        
        http_serve(ssl,s);
        exit(0);
      }
    }
    destroy_ctx(ctx);
    exit(0);
  }
