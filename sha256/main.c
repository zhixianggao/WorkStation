int main()
{
    uint8_t buf_data[32];//encrypted buff
    unsigned char c_data[] = {"abcd"};//buff to be encrypted
 
    sha256_context_t ctx_data;
    sha256_init(&ctx_data);
    sha256_update(&ctx_data, c_data, sizeof(c_data));
    sha256_final(&ctx_data, buf_data);//256bit(32byte abstract)
}
