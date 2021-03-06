#include <stdio.h>
#include "ssl_locl.h"
#include "openssl/ssl.h"
#ifndef OPENSSL_NO_COMP
#include <openssl/comp.h>
#endif
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#ifdef KSSL_DEBUG
#include <openssl/des.h>
#endif

int spp_enc(SSL *s, int send) {
    SPP_SLICE *slice;
    
    if (!(SSL_in_init(s) || s->in_handshake)) {
        if (send) {
            slice = s->write_slice;
        } else {
            slice = s->read_slice;
        }
        // Error if a slice has not been specified for this encrypt/decrypt op
        if (slice == NULL) {
            SSLerr(SSL_F_SPP_ENC,SPP_R_MISSING_SLICE);
            return -1;
        }

        /* If we do not possess the encryption material for this slice, 
         * do not attempt to decrypt. Not Needed, see below. */
        //if (!slice->have_material) {
            /* Copy the still encrypted content to the correct location. */
        //    return 1;
        //}

        /* Pick the right slice, and encrypt with it. */
        /* If we do not have the encryption material, slice->enc_XXX_ctx should be null. 
         * In that case, tls1 applies the null cipher. */
        if (send) {
            s->enc_write_ctx = slice->read_ciph->enc_write_ctx;
        } else if (!send) {
            s->enc_read_ctx = slice->read_ciph->enc_read_ctx;
        }
    }
    return tls1_enc(s, send);
}

int xor_array(unsigned char* dst, unsigned char* src1, unsigned char* src2, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        *(dst++) = *(src1++) ^ *(src2++);
    }
    return 1;
}

int spp_init_slice_st(SSL *s, SPP_SLICE *slice, int which) {
    const EVP_CIPHER *c;
    const EVP_MD *m;    
    int is_exp,cl,k;
    unsigned char key_ex[EVP_MAX_KEY_LENGTH];
    unsigned char iv_ex[EVP_MAX_KEY_LENGTH];
    unsigned char *key, *iv;
    int mac_type;
    EVP_PKEY *mac_key;
    EVP_MD_CTX md;
    mac_type = s->s3->tmp.new_mac_pkey_type;
    m=s->s3->tmp.new_hash;
    key = &(key_ex[0]);
    iv = &(iv_ex[0]);
    is_exp=SSL_C_IS_EXPORT(s->s3->tmp.new_cipher);    
    c=s->s3->tmp.new_sym_enc;
    
    cl=EVP_CIPHER_key_length(c);
    k=EVP_CIPHER_iv_length(c);
    //printf("Init slice %d\n", slice->slice_id);
    
    if (which & SSL3_CC_READ) {
        //printf("which=read\n");
        if (slice->read_access) {
            // Secret is computed by XORing the material generated by the client and server
            xor_array(key, slice->read_mat, slice->other_read_mat, EVP_MAX_KEY_LENGTH);

            // Generate the encryption contexts.
            //printf("encryption init\n");
            if (slice->read_ciph == NULL) {
                if ((slice->read_ciph=OPENSSL_malloc(sizeof(SPP_CIPH))) == NULL)
                    goto err;
            }
            if ((slice->read_ciph->enc_read_ctx=OPENSSL_malloc(sizeof(EVP_CIPHER_CTX))) == NULL)
                goto err;
            EVP_CIPHER_CTX_init(slice->read_ciph->enc_read_ctx);
            EVP_CipherInit_ex(slice->read_ciph->enc_read_ctx,c,NULL,key,iv,(which & SSL3_CC_WRITE));

            // And the read mac contexts

            //printf("read mac init\n");
            if ((slice->read_mac=spp_init_mac_st(s, slice->read_mac, key, which)) == NULL) {
                goto err;
            }
        } else {
            if (slice->read_ciph == NULL) {
                if ((slice->read_ciph=OPENSSL_malloc(sizeof(SPP_CIPH))) == NULL)
                    goto err;
            }
            slice->read_ciph->enc_read_ctx = NULL;
        }
        if (slice->write_access) {
            xor_array(key, slice->write_mat, slice->other_write_mat, EVP_MAX_KEY_LENGTH);

            // Generate the write mac context

            //printf("write mac init\n");
            if ((slice->write_mac=spp_init_mac_st(s, slice->write_mac, key, which)) == NULL) {
                goto err;
            }
        }
    } else {
        //printf("which=write\n");
        if (slice->read_access) {
            // Secret is computed by XORing the material generated by the client and server
            xor_array(key, slice->read_mat, slice->other_read_mat, EVP_MAX_KEY_LENGTH);

            // Generate the encryption contexts.

            if (slice->read_ciph == NULL) {
                if ((slice->read_ciph=OPENSSL_malloc(sizeof(SPP_CIPH))) == NULL)
                    goto err;
            }

            //printf("encryption init\n");
            if ((slice->read_ciph->enc_write_ctx=OPENSSL_malloc(sizeof(EVP_CIPHER_CTX))) == NULL)
                goto err;
            EVP_CIPHER_CTX_init(slice->read_ciph->enc_write_ctx);
            EVP_CipherInit_ex(slice->read_ciph->enc_write_ctx,c,NULL,key,iv,(which & SSL3_CC_WRITE));

            // And the read mac contexts

            //printf("read mac init\n");
            if ((slice->read_mac=spp_init_mac_st(s, slice->read_mac, key, which)) == NULL) {
                goto err;
            }
        } else {
            if (slice->read_ciph == NULL) {
                if ((slice->read_ciph=OPENSSL_malloc(sizeof(SPP_CIPH))) == NULL)
                    goto err;
            }
            slice->read_ciph->enc_write_ctx = NULL;
        }
        if (slice->write_access) {            
            xor_array(key, slice->write_mat, slice->other_write_mat, EVP_MAX_KEY_LENGTH);

            // Generate the write mac context

            //printf("write mac init\n");
            if ((slice->write_mac=spp_init_mac_st(s, slice->write_mac, key, which)) == NULL) {
                goto err;
            }
        }
    }
    return 1;
err:
    printf("Error in slice init\n");
    return -1;
}

SPP_MAC* spp_init_mac_st(SSL* s, SPP_MAC* mac, unsigned char* key, int which) {
    int mac_type;
    EVP_PKEY *mac_key;
    EVP_MD_CTX md;
    const EVP_MD *m;
    
    mac_type = s->s3->tmp.new_mac_pkey_type;
    m=s->s3->tmp.new_hash;
    
    if (mac == NULL) {
        if ((mac=OPENSSL_malloc(sizeof(SPP_MAC))) == NULL) {
            return NULL;
        }
    }
    if (which & SSL3_CC_READ) {
        mac->read_hash = EVP_MD_CTX_create();
        //ssl_replace_hash(&(mac->read_hash),NULL);
        memset(&(mac->read_sequence[0]),0,8);
        mac->read_mac_secret_size = s->s3->tmp.new_mac_secret_size;
        OPENSSL_assert(mac->read_mac_secret_size <= EVP_MAX_MD_SIZE);
        memcpy(&(mac->read_mac_secret[0]), key, mac->read_mac_secret_size);
        mac_key = EVP_PKEY_new_mac_key(mac_type, NULL,&(mac->read_mac_secret[0]),mac->read_mac_secret_size);
        EVP_DigestSignInit(mac->read_hash,NULL,m,NULL,mac_key);
        EVP_PKEY_free(mac_key);
    } else {
        mac->write_hash = EVP_MD_CTX_create();
        //ssl_replace_hash(&(mac->write_hash),NULL);
        memset(&(mac->write_sequence[0]),0,8);
        mac->write_mac_secret_size = s->s3->tmp.new_mac_secret_size;
        OPENSSL_assert(mac->write_mac_secret_size <= EVP_MAX_MD_SIZE);
        memcpy(&(mac->write_mac_secret[0]), key, mac->write_mac_secret_size);
        mac_key = EVP_PKEY_new_mac_key(mac_type, NULL,&(mac->write_mac_secret[0]),mac->write_mac_secret_size);
        EVP_DigestSignInit(mac->write_hash,NULL,m,NULL,mac_key);
        EVP_PKEY_free(mac_key);
    }
    
    return mac;
}

int spp_init_slices_st(SSL *s, int which) {
    int i;
    for (i = 0; i < s->slices_len; i++) {
        if (spp_init_slice_st(s, s->slices[i], which) <= 0)
            return -1;
    }
    return 1;
}

int spp_init_integrity_st(SSL *s) {
    /*if (s->i_mac == NULL) {
        if ((s->i_mac=(SPP_MAC*)OPENSSL_malloc(sizeof(SPP_MAC)))==NULL)
            goto err;
        memset(&(s->i_mac->read_sequence[0]),0,8);
        memset(&(s->i_mac->write_sequence[0]),0,8);
        s->i_mac->read_mac_secret_size = s->s3->read_mac_secret_size;
        s->i_mac->write_mac_secret_size = s->s3->write_mac_secret_size;
        memcpy(&(s->i_mac->read_mac_secret[0]), &(s->s3->read_mac_secret[0]), s->s3->read_mac_secret_size);
        memcpy(&(s->i_mac->write_mac_secret[0]), &(s->s3->write_mac_secret[0]), s->s3->write_mac_secret_size); 
        s->i_mac->read_hash = s->read_hash;
        s->i_mac->write_hash = s->write_hash;
    }*/
    
    return 1;
err:
    return -1;
}

int spp_store_defaults(SSL *s, int which) {
    if (which & SSL3_CC_READ) {
        // MAC ctx
        memset(&(s->def_ctx->read_mac->read_sequence[0]),0,8);
        s->def_ctx->read_mac->read_mac_secret_size = s->s3->read_mac_secret_size;
        memcpy(&(s->def_ctx->read_mac->read_mac_secret[0]), &(s->s3->read_mac_secret[0]), s->s3->read_mac_secret_size);
        s->def_ctx->read_mac->read_hash = s->read_hash;
        s->def_ctx->read_access = 1;
        
        // Encrypt ctx
        s->def_ctx->read_ciph->enc_read_ctx = s->enc_read_ctx;
    } else {
        // MAC ctx
        memset(&(s->def_ctx->read_mac->write_sequence[0]),0,8);
        s->def_ctx->read_mac->write_mac_secret_size = s->s3->write_mac_secret_size;
        memcpy(&(s->def_ctx->read_mac->write_mac_secret[0]), &(s->s3->write_mac_secret[0]), s->s3->write_mac_secret_size); 
        s->def_ctx->read_mac->write_hash = s->write_hash;
        s->def_ctx->write_access = 1;
        
        // Encrypt ctx
        s->def_ctx->read_ciph->enc_write_ctx = s->enc_write_ctx;
    }
    return 1;
}

int spp_change_cipher_state(SSL *s, int which) {
    int ret=1;
    if (!s->proxy) {
        ret = tls1_change_cipher_state(s, which);
        if (ret <= 0) goto end;
        ret = spp_store_defaults(s, which);
        if (ret <= 0) goto end;
    }
    //ret = spp_init_slices_st(s, which);
    //if (ret <= 0) goto end;
end:
    return ret;
}