/* File: serverCommunication.h */

#include <string>
#include "serverTypes.h"

int traverse_directory(std::string path, sock_info_t sock_info, int relative_path_size);

void *communication_thread(void *void_t_socket);