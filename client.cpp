/******************************************************//*
Creators: Matthew Pisini, Charles Bennett
Date: 9/19/20
Description:
Inputs:
1. Destination IP address
2. Port # to listen on
3. Destination port # to send to
4. Name of the file to save incoming data to
*//******************************************************/
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <pthread.h>
#include<chrono>
#include "client.h"

#define HEADER_SIZE (2)                            //Total number of bytes per packet in the header 
#define PACKET_SIZE (1500)                         //Optional parameter for use
#define NUM_ACKS (100)                              //Number of ACKs per packet (each ACK is 2 byte packet ID) 
#define ACK_WINDOW (5)                             //Sliding window of duplicate ACK transmissions

/*************** CONTROL FIELDS *******************/

#define FIELD1_SIZE (2)                            //Packet Size
#define FIELD2_SIZE (2)                            //# of Packets in Transmission
#define NUM_CONTROL_FIELDS (2)                     //# Fields in control header
#define NUM_PACKETS_EXPECTED (50991)               //Hardcoded Packet Size (comment if control packet in use)
int control_field_array[NUM_CONTROL_FIELDS];       //Array to store the decoded control fields
int control_field_sizes[NUM_CONTROL_FIELDS]        //Define sizes of control fields
    = {FIELD1_SIZE, FIELD1_SIZE};

/**************************************************/

using namespace std;

/************************************** CONSTRUCTOR ****************************************/
client_listen::client_listen(char* dest_ip_address, char* listen_port, char* dest_port) :
    UDP(dest_ip_address, listen_port, dest_port)
{
    this->num_packets_expected = NUM_PACKETS_EXPECTED;
    this->num_packets_received = 0;
    this->first_packet = false;
    queue< vector<char> > packet_queue;
    vector< vector<char> > ACK_queue;
    queue< vector <char>> packet_ID_list;
    this->packet_ID_list_size = 0;
    pthread_mutex_init(&this->packet_lock, NULL);
    pthread_mutex_unlock(&this->packet_lock);
}
/**********************************************************************************************/

/************************************** PAYLOAD MANAGEMENT (MAP) ****************************************/
//add data to the array
void client_listen::map_add(int packet_number, vector<char> data)
{
    vector<char> payload = data;
    payload.erase(payload.begin(), payload.begin() + 2);
    if (!this->data_map.count(packet_number))
    {
        this->num_packets_received++;
    }
    this->data_map.insert(std::pair<int, vector<char>>(packet_number, payload));
}
/**********************************************************************************************/
//print data array
void client_listen::print_data_map()
{
    vector<char>::iterator it;
    cout << "(packet ID, data)" << endl;
    for (const auto& x : this->data_map)
    {

        cout << x.first << ": ";// << x.second << endl;
        vector<char> items = x.second;
        for (it = items.begin(); it != items.end(); it++)
        {
            cout << *it;
        }
        cout << endl;
    }
}
/**********************************************************************************************/

/************************************** PACKET PROCESSING ****************************************/
void client_listen::process_packet(vector<char> packet)
{
    // int packet_ID = strip_header(packet);
    //strip_header
    unsigned char input[HEADER_SIZE];
    vector<char> ACK_input;
    for (int i = 0; i < HEADER_SIZE; i++)
    {
        input[i] = packet[i];
        ACK_input.push_back(packet[i]);
    }
    int packet_ID = bytes_to_int(input, HEADER_SIZE);

    //add payload to map and packet_ID_list if it is a unique packet ID
    vector<char> payload = packet;
    payload.erase(payload.begin(), payload.begin() + 2);
    if (!this->data_map.count(packet_ID))
    {
        this->num_packets_received++;
        // cout << "Total packets received: " << this->num_packets_received << endl;
        this->packet_ID_list.push(ACK_input);
        this->packet_ID_list_size++;
        this->data_map.insert(std::pair<int, vector<char>>(packet_ID, payload));
    }

    // map_add(packet_ID, packet);              //send payload
}
//strips control info from header
int client_listen::strip_header(vector<char> data)
{
    unsigned char input[HEADER_SIZE];
    vector<char> ACK_input;
    for (int i = 0; i < HEADER_SIZE; i++)
    {
        input[i] = data[i];
        ACK_input.push_back(data[i]);
    }
    int packet_ID = bytes_to_int(input, HEADER_SIZE);
    // this->packet_ID_list.push(ACK_input);
    // this->packet_ID_list_size++;
    return packet_ID;
}
//processing of control packet (first transmission)
void client_listen::control_packet(vector<char> data)
{
    int offset = 0;
    for (int i = 0; i < NUM_CONTROL_FIELDS; i++)
    {
        unsigned char input[control_field_sizes[i]];
        for (int j = 0; j < control_field_sizes[i]; j++)
        {
            input[j] = data[j + offset];
        }
        control_field_array[i] = bytes_to_int(input, control_field_sizes[i]);
        offset += control_field_sizes[i];
    }

    cout << "FIELD1: " << control_field_array[0] << endl;
    cout << "FIELD2: " << control_field_array[1] << endl;

    this->packet_size = control_field_array[0];             //FIELD1 = packet size
    this->num_packets_expected = control_field_array[1];    //FIELD2 = num packets
    this->first_packet = false;
}
/**********************************************************************************************/

/************************************ PACKET TRANSMISSION **************************************/
//Bundle ACKS into single payload for transmission
void client_listen::create_ACK_packet(int ACK_packet_size)
{
    vector<char> output_packet;
    vector<char>::iterator it;
    if (this->packet_ID_list.size() < ACK_packet_size)
    {
        cout << "error over indexing ack creation vector" << endl;
    }
    for (int i = 0; i < ACK_packet_size; i++)
    {
        int j = 0;
        for (it = this->packet_ID_list.front().begin(); it != this->packet_ID_list.front().end(); it++)
        {
            output_packet.push_back(*it);
        }
        this->packet_ID_list.pop();
    }
    // cout << "packet_ID_list.size(): " << this->packet_ID_list.size() << endl;
    this->ACK_queue.push_back(output_packet);
}

void client_listen::send_ACKs(int index)
{
    int temp;
    if (this->ACK_queue.size() <= ACK_WINDOW)
    {
        temp = (index + 1);
        index = 0;
    }
    else
    {
        temp = index + 1;
        index = (index + 1) - ACK_WINDOW;
    }
    for (vector<vector<char>>::iterator it = (this->ACK_queue.begin() + index);
            it != (this->ACK_queue.begin() + temp); ++it)
    {
        unsigned char* output;
        output = (unsigned char*)vector_to_cstring(*it);
        /* DEBUG
        cout << "output: ";
        int j = 0;
        for(int i = 0; i < it->size(); i+=2)
        {
            // j = output[i] | output[i+1] << 8;
            unsigned char f[2] = {output[i],output[i+1]};
            j = bytes_to_int(f,2);
            cout << j << endl;
        }
        cout << endl;*/
        cout << "sending ACK Packet #: " << distance(this->ACK_queue.begin(), it) << endl;
        this->send((char*)output);
        delete [] output;
    }
}

void listener(char* dest_ip_address, char* listen_port, char* dest_port, char* output_file)
{
    client_listen client(dest_ip_address, listen_port, dest_port);
    int thread_num, byte_size, rc;
    void* status;
    pthread_t processing_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    client.setPacketSize(NUM_ACKS * HEADER_SIZE);
    cout << "creating processing thread..." << endl;
    thread_num = pthread_create(&processing_thread, &attr, &empty_packet_queue, (void*)&client);
    pthread_attr_destroy(&attr);
    while (1)
    {
        // char packet_ID[HEADER_SIZE];
        // std::cout << "listening for packet..." << endl;
        char* temp = client.recieve(byte_size);
        pthread_mutex_lock(&client.packet_lock);
        vector<char> thread_buffer = cstring_to_vector(temp, byte_size);

        if (client.num_packets_received >= client.num_packets_expected) //have all the packets
        {
            rc = pthread_join(processing_thread, &status);
            if (rc)
            {
                std::cout << "ERRROR: joining threads" << std::endl;
                exit(1);
            }
            write_to_file(client.data_map, output_file);
            time_t time_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::cout << "Transmission complete at " << time_now << std::endl;
            // client.print_data_map();
            pthread_exit(NULL);
        }
        //first packet should be control
        // if (client.first_packet)
        // {
        //     // client.control_packet(thread_buffer);
        // }
        // else
        // {
        // for(auto it = thread_buffer.begin(); it != thread_buffer.end(); ++it)
        // {
        //     cout << *it;
        // }
        // cout << endl;
        // }
        // cout << "thread_buffer size: " << thread_buffer.size() << endl;
        unsigned char input[HEADER_SIZE];
        vector<char> ACK_input;
        for (int i = 0; i < HEADER_SIZE; i++)
        {
            input[i] = thread_buffer[i];
        }
        int packet_ID = bytes_to_int(input, HEADER_SIZE);
        if (!client.data_map.count(packet_ID))
        {
            client.packet_queue.push(thread_buffer);
        }
        pthread_mutex_unlock(&client.packet_lock);
    }

}

int main(int argc, char const* argv[])
{
    if (argc < 5)
    {
        std::cout << "Need more information: (DEST_IP, LISTEN_PORT, DEST_PORT, OUTPUT_FILE)." << endl;
        exit(1);
    }

    char* DEST_IP = (char*)argv[1];
    char* LISTEN_PORT = (char*)argv[2];
    char* DEST_PORT = (char*)argv[3];
    char* output_file = (char*)argv[4];
    listener(DEST_IP, LISTEN_PORT, DEST_PORT, output_file);
    // client_listen client(DEST_IP, LISTEN_PORT, DEST_PORT);
    // vector<char> input = {'a','a','b','b'};
    // client.map_add(1, input);
    // input = {'a','a','b','b'};
    // client.map_add(2, input);
    // input = {'a','a','b','b'};
    // client.map_add(1, input);

    return 0;
}

void write_to_file(std::map<int, std::vector <char>> map, char* file_name)
{
    ofstream file;
    file.open(file_name);
    vector<char>::iterator it;
    for (const auto& x : map)
    {
        vector<char> items = x.second;
        for (it = items.begin(); it != items.end(); it++)
        {
            file << *it;
        }
    }
    file.close();
}

void* empty_packet_queue(void* input)
{
    class client_listen* client = static_cast<class client_listen*>(input);
    int index = 0;
    while (1)
    {
        // cout<<"packet queue size" << client->packet_queue.size() << endl;
        if (client->packet_ID_list.size() >= NUM_ACKS)
        {
            // cout << "creating packet" << endl;
            client->create_ACK_packet(NUM_ACKS);
            client->send_ACKs(index);
            index++;

        }
        if (client->packet_queue.size() > 0) //packets still to be processed
        {
            //Have not received all of the unique packets wee expect tot receive
            if (client->num_packets_received < client->num_packets_expected)
            {
                pthread_mutex_lock(&client->packet_lock);
                vector<char> packet = client->packet_queue.front();
                client->process_packet(packet);
                client->packet_queue.pop();
                pthread_mutex_unlock(&client->packet_lock);
            }

            //Received all of the packets we expected to receive --> done processing
            if ( (client->num_packets_received >= client->num_packets_expected) && !(client->first_packet) )
            {
                cout << "creating final packet" << endl;
                client->create_ACK_packet(client->packet_ID_list.size());
                for (int i = 0; i < ACK_WINDOW; i++)
                {
                    client->send_ACKs(index);   //make sure last packet is sent ACK_WINDOW times
                }
                pthread_t pthread_self(void);
                cout << "killing thread" << endl;
                pthread_exit(NULL);
            }
        }
        // else
        // {
        //     sleep(0.01); //optional sleep parameter
        // }
    }
};

char* vector_to_cstring(vector<char> input)
{
    char* output = new char[input.size()];
    for (int i = 0; i < input.size(); i++)
    {
        output[i] = input[i];
    }
    return output;
}
vector<char> cstring_to_vector(char* input, int size)
{
    vector<char> output(size);
    for (int i = 0; i < size; i++)
    {
        output[i] = input[i];
    }
    return output;
}
