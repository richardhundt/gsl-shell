#include <pthread.h>
#include <stdio.h>

#include "gsl_shell_thread.h"

extern "C" void * luajit_eval_thread (void *userdata);

void *
luajit_eval_thread (void *userdata)
{
  gsl_shell_thread* eng = (gsl_shell_thread*) userdata;
  eng->lock();
  eng->init();
  eng->run();
  pthread_exit(NULL);
  return NULL;
}

gsl_shell_thread::gsl_shell_thread():
  m_status(starting), m_redirect(4096), m_exit_request(false)
{
}

gsl_shell_thread::~gsl_shell_thread()
{
  m_redirect.stop();
}

void gsl_shell_thread::start()
{
  m_redirect.start();

  pthread_attr_t attr[1];

  pthread_attr_init (attr);
  pthread_attr_setdetachstate (attr, PTHREAD_CREATE_DETACHED);

  if (pthread_create (&m_thread, attr, luajit_eval_thread, (void*)this))
    {
      fprintf(stderr, "error creating thread");
    }
}

void
gsl_shell_thread::run()
{
  while (1)
    {
      m_eval.lock();
      m_status = ready;

      this->unlock();
      fprintf(stderr, "GSL SHELL LOOP: waiting EVAL signal...\n");
      m_eval.wait();
      this->lock();

      before_eval();

      if (m_exit_request)
	{
	  m_eval.unlock();
	  break;
	}

      m_status = busy;
      fprintf(stderr, "GSL SHELL LOOP: unlocking EVAL mutex...\n");
      m_eval.unlock();
      fprintf(stderr, "GSL SHELL LOOP: unlocked.\n");

      // here m_line_pending cannot be modified by the other thread
      // because we declared above m_status to "busy" befor unlocking m_eval
      const char* line = m_line_pending.cstr();
      m_eval_status = this->exec(line);

      fputc(eot_character, stdout);
      fflush(stdout);
    }

  this->unlock();
  this->close();
}

void
gsl_shell_thread::stop()
{
  pthread::auto_lock lock(m_eval);
  m_exit_request = true;
  m_eval.signal();
  pthread_join(m_thread, NULL);
}

void
gsl_shell_thread::input(const char* line)
{
  pthread::auto_lock lock(m_eval);

  if (m_status == ready)
    {
      m_line_pending = line;
      fprintf(stderr, "GSL SHELL INPUT: sending EVAL signal...\n");
      m_eval.signal();
      fprintf(stderr, "GSL SHELL INPUT: EVAL signal sent.\n");
    }
}

int
gsl_shell_thread::read(char* buffer, unsigned buffer_size)
{
  return m_redirect.read(buffer, buffer_size);
}