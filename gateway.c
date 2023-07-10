#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <semaphore.h>
#include "thpool.h"

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define PORT_NUMBER 8080
#define BUFFER_CAPACITY 10

typedef struct
{
    int identifier;
    float temperature;
    float humidity;
    char *timestamp;
} SensorData;

typedef struct
{
    SensorData *buffer;
    int first;
    int last;
    pthread_mutex_t mutex;
    sem_t spaces;
    sem_t items;
} BoundedBuffer;

typedef struct
{
    int client_socket;
    SensorData sensor_data;
} ClientInfo;

BoundedBuffer *bounded_buffer;

BoundedBuffer *bounded_buffer_init()
{
    BoundedBuffer *buffer = (BoundedBuffer *)malloc(sizeof(BoundedBuffer));
    buffer->buffer = (SensorData *)malloc(sizeof(SensorData) * BUFFER_CAPACITY);
    buffer->first = 0;
    buffer->last = 0;
    pthread_mutex_init(&buffer->mutex, NULL);
    sem_init(&buffer->spaces, 0, BUFFER_CAPACITY);
    sem_init(&buffer->items, 0, 0);
    return buffer;
}

void bounded_buffer_enqueue(SensorData data)
{
    sem_wait(&bounded_buffer->spaces);
    pthread_mutex_lock(&bounded_buffer->mutex);
    printf("Enqueued message from node #%d: %.2f, %.2f, %s\n", data.identifier, data.temperature, data.humidity, data.timestamp);
    bounded_buffer->buffer[bounded_buffer->last] = data;
    bounded_buffer->last = (bounded_buffer->last + 1) % BUFFER_CAPACITY;
    pthread_mutex_unlock(&bounded_buffer->mutex);
    sem_post(&bounded_buffer->items);
}

void bounded_buffer_dequeue()
{
    sem_wait(&bounded_buffer->items);
    pthread_mutex_lock(&bounded_buffer->mutex);

    SensorData data = bounded_buffer->buffer[bounded_buffer->first];
    printf("Dequeued message from node #%d: %.2f, %.2f, %s\n", data.identifier, data.temperature, data.humidity, data.timestamp);
    bounded_buffer->first = (bounded_buffer->first + 1) % BUFFER_CAPACITY;

    pthread_mutex_unlock(&bounded_buffer->mutex);
    sem_post(&bounded_buffer->spaces);
}

void bounded_buffer_destroy(BoundedBuffer *buffer)
{
    pthread_mutex_destroy(&buffer->mutex);
    sem_destroy(&buffer->spaces);
    sem_destroy(&buffer->items);
    free(buffer->buffer);
    free(buffer);
}

void bounded_buffer_consume(void *arg)
{
    while (1)
    {
        bounded_buffer_dequeue();
        sleep(6);
    }
}

char *convert_time(time_t raw_time)
{
    struct tm *time_info;
    time_info = localtime(&raw_time);
    char *time_string = malloc(sizeof(char) * 9);
    strftime(time_string, sizeof(char) * 9, "%H:%M:%S", time_info);
    return time_string;
}

SensorData parse_message(char *message)
{
    SensorData sensor_data;
    char *token = strtok(message, ",");
    int i = 0;
    while (token != NULL)
    {
        switch (i)
        {
        case 0:
            sensor_data.identifier = atoi(token);
            break;
        case 1:
            sensor_data.temperature = atof(token);
            break;
        case 2:
            sensor_data.humidity = atof(token);
            break;
        case 3:
            sensor_data.timestamp = convert_time((time_t)atof(token));
            break;
        default:
            break;
        }
        token = strtok(NULL, ",");
        i++;
    }
    return sensor_data;
}

void handle_client(void *arg)
{
    ClientInfo *client_info = (ClientInfo *)arg;
    int client_socket = client_info->client_socket;

    char raw_message[BUFFER_SIZE];
    memset(raw_message, 0, BUFFER_SIZE);

    // Read data from the client
    ssize_t bytes_read = recv(client_socket, raw_message, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0)
    {
        fprintf(stderr, "Error reading data from client #%lu\n", pthread_self());
    }
    // Parse the received message
    client_info->sensor_data = parse_message(raw_message);
    // Print the received message
    printf("Received message from node #%d: %.2f, %.2f, %s\n", client_info->sensor_data.identifier, client_info->sensor_data.temperature, client_info->sensor_data.humidity, client_info->sensor_data.timestamp);
    // Add the received message to the buffer
    bounded_buffer_enqueue(client_info->sensor_data);

    // Close the client socket
    close(client_socket);
    // Free the client info
    free(client_info);
}

int main()
{
    // Create a bounded buffer with a size of 10
    bounded_buffer = bounded_buffer_init();

    // Create a thread pool with 5 threads
    threadpool thpool = thpool_init(5);

    // Create a server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Error creating server socket");
        return 1;
    }

    // Bind the server socket to a port
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT_NUMBER);
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("Error binding server socket to port");
        close(server_socket);
        return 1;
    }

    // Listen for client connections
    if (listen(server_socket, MAX_CLIENTS) < 0)
    {
        perror("Error listening for client connections");
        close(server_socket);
        return 1;
    }
    printf("Waiting for client connections on port %d...\n", PORT_NUMBER);
    thpool_add_work(thpool, bounded_buffer_consume, (void *)NULL);

    // Accept client connections and handle them
    while (1)
    {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0)
        {
            perror("Error accepting client connection");
            continue;
        }

        // Create a ClientInfo struct to pass to the thread
        ClientInfo *client_info = (ClientInfo *)malloc(sizeof(ClientInfo));
        client_info->client_socket = client_socket;

        // Add a task to the thread pool to handle the client
        thpool_add_work(thpool, handle_client, (void *)client_info);
    }

    // Destroy the bounded buffer
    bounded_buffer_destroy(bounded_buffer);

    // Wait for all tasks to finish
    thpool_wait(thpool);

    // Destroy the thread pool
    thpool_destroy(thpool);

    // Close the server socket
    close(server_socket);

    return 0;
}
