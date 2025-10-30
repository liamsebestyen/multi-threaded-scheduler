This program simulates trains scheduling trains arriving from both the West and the East, with high, and low priorities, while managing the trains crossing one track. Each train is represented by a thread, and synchronization is achieved using mutexes and condition variables.  
The program ensures that only one train is on the track at a time, and the priority of which train gets to cross the track is determined by the rules set out for the specification for this assignment.

All outputs are written to an output file named `output.txt`.

This implementation of the assignment satisfies all of the requirements of the aspecification as follows:

1. Only one train is on the main track at any given time

2. Only loaded trains can cross the main track

3. Generally (not always) high-priority trains cross before low-priority trains  
4. If two loaded trains of equal priority
   - Same direction: The one that finished loading first crosses first, and if that is identical then it is whatever train is first on the read file.
   - Opposite direction:The train that yields alternating crossing directions will go first, and if no train has gone yet, West is best!
5. Starvation prevention:
   - If two trains travel in the same direction consecutively, a waiting train from the opposite direction must follow regarldess of it's priority. 
This is regardless of the priortiy ;however, if there is no train, we may continue normall as usual


There is a sample file to read the trains from titled 'input.txt'. 
The estimated run time of this file is approximately two seconds to run and write to the output.txt